// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomSettings/LyraSettingAction_SafeZoneEditor.h"
#include "CustomSettings/LyraSettingValueDiscrete_MobileFPSType.h"
#include "CustomSettings/LyraSettingValueDiscrete_OverallQuality.h"
#include "CustomSettings/LyraSettingValueDiscrete_Resolution.h"
#include "DataSource/GameSettingDataSource.h"
#include "EditCondition/WhenCondition.h"
#include "EditCondition/WhenPlatformHasTrait.h"
#include "EditCondition/WhenPlayingAsPrimaryPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameSettingCollection.h"
#include "GameSettingValueDiscreteDynamic.h"
#include "LyraGameSettingRegistry.h"
#include "LyraSettingsLocal.h"
#include "LyraSettingsShared.h"
#include "NativeGameplayTags.h"
#include "Performance/LyraPerformanceSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "NvidiaSettingsManagerInterface.h"
#include "DLSSLibrary.h"
#include "Player/LyraLocalPlayer.h"

#define LOCTEXT_NAMESPACE "Lyra"

UE_DEFINE_GAMEPLAY_TAG_STATIC(GameSettings_Action_EditSafeZone, "GameSettings.Action.EditSafeZone");
UE_DEFINE_GAMEPLAY_TAG_STATIC(GameSettings_Action_EditBrightness, "GameSettings.Action.EditBrightness");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Platform_Trait_SupportsWindowedMode, "Platform.Trait.SupportsWindowedMode");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Platform_Trait_NeedsBrightnessAdjustment, "Platform.Trait.NeedsBrightnessAdjustment");

//////////////////////////////////////////////////////////////////////

enum class EFramePacingEditCondition
{
	EnableIf,
	DisableIf
};

// Checks the platform-specific value for FramePacingMode
class FGameSettingEditCondition_FramePacingMode : public FGameSettingEditCondition
{
public:
	FGameSettingEditCondition_FramePacingMode(ELyraFramePacingMode InDesiredMode, EFramePacingEditCondition InMatchMode = EFramePacingEditCondition::EnableIf)
		: DesiredMode(InDesiredMode)
		, MatchMode(InMatchMode)
	{
	}

	virtual void GatherEditState(const ULocalPlayer * InLocalPlayer, FGameSettingEditableState & InOutEditState) const override
	{
		const ELyraFramePacingMode ActualMode = ULyraPlatformSpecificRenderingSettings::Get()->FramePacingMode;
		
		const bool bMatches = (ActualMode == DesiredMode);
		const bool bMatchesAreBad = (MatchMode == EFramePacingEditCondition::DisableIf);

		if (bMatches == bMatchesAreBad)
		{
			InOutEditState.Kill(FString::Printf(TEXT("Frame pacing mode %d didn't match requirement %d"), (int32)ActualMode, (int32)DesiredMode));
		}
	}
private:
	ELyraFramePacingMode DesiredMode;
	EFramePacingEditCondition MatchMode;
};

//////////////////////////////////////////////////////////////////////

// Checks the platform-specific value for bSupportsGranularVideoQualitySettings
class FGameSettingEditCondition_VideoQuality : public FGameSettingEditCondition
{
public:
	FGameSettingEditCondition_VideoQuality(const FString& InDisableString)
		: DisableString(InDisableString)
	{
	}

	virtual void GatherEditState(const ULocalPlayer* InLocalPlayer, FGameSettingEditableState& InOutEditState) const override
	{
		if (!ULyraPlatformSpecificRenderingSettings::Get()->bSupportsGranularVideoQualitySettings)
		{
			InOutEditState.Kill(DisableString);
		}
	}

	virtual void SettingChanged(const ULocalPlayer* LocalPlayer, UGameSetting* Setting, EGameSettingChangeReason Reason) const override
	{
		// TODO for now this applies the setting immediately
		const ULyraLocalPlayer* LyraLocalPlayer = CastChecked<ULyraLocalPlayer>(LocalPlayer);
		LyraLocalPlayer->GetLocalSettings()->ApplyScalabilitySettings();
	}

private:
	FString DisableString;
};

////////////////////////////////////////////////////////////////////////////////////

UGameSettingCollection* ULyraGameSettingRegistry::InitializeVideoSettings(ULyraLocalPlayer* InLocalPlayer)
{
	UGameSettingCollection* Screen = NewObject<UGameSettingCollection>();
	Screen->SetDevName(TEXT("VideoCollection"));
	Screen->SetDisplayName(LOCTEXT("VideoCollection_Name", "Video"));
	Screen->Initialize(InLocalPlayer);

	UGameSettingValueDiscreteDynamic_Enum* WindowModeSetting = nullptr;
	UGameSetting* MobileFPSType = nullptr;
	UGameSettingValueDiscreteDynamic_Enum* NvidiaDLSSModeDependency = nullptr;

	// Display
	////////////////////////////////////////////////////////////////////////////////////
	{
		UGameSettingCollection* Display = NewObject<UGameSettingCollection>();
		Display->SetDevName(TEXT("DisplayCollection"));
		Display->SetDisplayName(LOCTEXT("DisplayCollection_Name", "Display"));
		Screen->AddSetting(Display);

		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Enum* Setting = NewObject<UGameSettingValueDiscreteDynamic_Enum>();
			Setting->SetDevName(TEXT("WindowMode"));
			Setting->SetDisplayName(LOCTEXT("WindowMode_Name", "Window Mode"));
			Setting->SetDescriptionRichText(LOCTEXT("WindowMode_Description", "In Windowed mode you can interact with other windows more easily, and drag the edges of the window to set the size. In Windowed Fullscreen mode you can easily switch between applications. In Fullscreen mode you cannot interact with other windows as easily, but the game will run slightly faster."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetFullscreenMode));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetFullscreenMode));
			Setting->AddEnumOption(EWindowMode::Fullscreen, LOCTEXT("WindowModeFullscreen", "Fullscreen"));
			Setting->AddEnumOption(EWindowMode::WindowedFullscreen, LOCTEXT("WindowModeWindowedFullscreen", "Windowed Fullscreen"));
			Setting->AddEnumOption(EWindowMode::Windowed, LOCTEXT("WindowModeWindowed", "Windowed"));

			Setting->AddEditCondition(FWhenPlatformHasTrait::KillIfMissing(TAG_Platform_Trait_SupportsWindowedMode, TEXT("Platform does not support window mode")));

			WindowModeSetting = Setting;

			Display->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			ULyraSettingValueDiscrete_Resolution* Setting = NewObject<ULyraSettingValueDiscrete_Resolution>();
			Setting->SetDevName(TEXT("Resolution"));
			Setting->SetDisplayName(LOCTEXT("Resolution_Name", "Resolution"));
			Setting->SetDescriptionRichText(LOCTEXT("Resolution_Description", "Display Resolution determines the size of the window in Windowed mode. In Fullscreen mode, Display Resolution determines the graphics card output resolution, which can result in black bars depending on monitor and graphics card. Display Resolution is inactive in Windowed Fullscreen mode."));

			Setting->AddEditDependency(WindowModeSetting);
			Setting->AddEditCondition(FWhenPlatformHasTrait::KillIfMissing(TAG_Platform_Trait_SupportsWindowedMode, TEXT("Platform does not support window mode")));
			Setting->AddEditCondition(MakeShared<FWhenCondition>([WindowModeSetting](const ULocalPlayer*, FGameSettingEditableState& InOutEditState)
			{
				if (WindowModeSetting->GetValue<EWindowMode::Type>() == EWindowMode::WindowedFullscreen)
				{
					InOutEditState.Disable(LOCTEXT("ResolutionWindowedFullscreen_Disabled", "When the Window Mode is set to <strong>Windowed Fullscreen</>, the resolution must match the native desktop resolution."));
				}
			}));

			Display->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			AddPerformanceStatPage(Display, InLocalPlayer);
		}
		//----------------------------------------------------------------------------------
	}

	// Graphics
	////////////////////////////////////////////////////////////////////////////////////
	{
		UGameSettingCollection* Graphics = NewObject<UGameSettingCollection>();
		Graphics->SetDevName(TEXT("GraphicsCollection"));
		Graphics->SetDisplayName(LOCTEXT("GraphicsCollection_Name", "Graphics"));
		Screen->AddSetting(Graphics);

		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Enum* Setting = NewObject<UGameSettingValueDiscreteDynamic_Enum>();
			Setting->SetDevName(TEXT("ColorBlindMode"));
			Setting->SetDisplayName(LOCTEXT("ColorBlindMode_Name", "Color Blind Mode"));
			Setting->SetDescriptionRichText(LOCTEXT("ColorBlindMode_Description", "Using the provided images, test out the different color blind modes to find a color correction that works best for you."));
			
			Setting->SetDynamicGetter(GET_SHARED_SETTINGS_FUNCTION_PATH(GetColorBlindMode));
			Setting->SetDynamicSetter(GET_SHARED_SETTINGS_FUNCTION_PATH(SetColorBlindMode));
			Setting->SetDefaultValue(GetDefault<ULyraSettingsShared>()->GetColorBlindMode());
			Setting->AddEnumOption(EColorBlindMode::Off, LOCTEXT("ColorBlindRotatorSettingOff", "Off"));
			Setting->AddEnumOption(EColorBlindMode::Deuteranope, LOCTEXT("ColorBlindRotatorSettingDeuteranope", "Deuteranope"));
			Setting->AddEnumOption(EColorBlindMode::Protanope, LOCTEXT("ColorBlindRotatorSettingProtanope", "Protanope"));
			Setting->AddEnumOption(EColorBlindMode::Tritanope, LOCTEXT("ColorBlindRotatorSettingTritanope", "Tritanope"));

			Setting->AddEditCondition(FWhenPlayingAsPrimaryPlayer::Get());

			Graphics->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
			Setting->SetDevName(TEXT("ColorBlindStrength"));
			Setting->SetDisplayName(LOCTEXT("ColorBlindStrength_Name", "Color Blind Strength"));
			Setting->SetDescriptionRichText(LOCTEXT("ColorBlindStrength_Description", "Using the provided images, test out the different strengths to find a color correction that works best for you."));

			Setting->SetDynamicGetter(GET_SHARED_SETTINGS_FUNCTION_PATH(GetColorBlindStrength));
			Setting->SetDynamicSetter(GET_SHARED_SETTINGS_FUNCTION_PATH(SetColorBlindStrength));
			Setting->SetDefaultValue(GetDefault<ULyraSettingsShared>()->GetColorBlindStrength());
			for (int32 Index = 0; Index <= 10; Index++)
			{
				Setting->AddOption(Index, FText::AsNumber(Index));
			}

			Setting->AddEditCondition(FWhenPlayingAsPrimaryPlayer::Get());

			Graphics->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueScalarDynamic* Setting = NewObject<UGameSettingValueScalarDynamic>();
			Setting->SetDevName(TEXT("Brightness"));
			Setting->SetDisplayName(LOCTEXT("Brightness_Name", "Brightness"));
			Setting->SetDescriptionRichText(LOCTEXT("Brightness_Description", "Adjusts the brightness."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetDisplayGamma));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetDisplayGamma));
			Setting->SetDefaultValue(2.2);
			Setting->SetDisplayFormat([](double SourceValue, double NormalizedValue) {
				return FText::Format(LOCTEXT("BrightnessFormat", "{0}%"), (int32)FMath::GetMappedRangeValueClamped(FVector2D(0, 1), FVector2D(50, 150), NormalizedValue));
			});
			Setting->SetSourceRangeAndStep(TRange<double>(1.7, 2.7), 0.01);

			Setting->AddEditCondition(FWhenPlayingAsPrimaryPlayer::Get());
			Setting->AddEditCondition(FWhenPlatformHasTrait::KillIfMissing(TAG_Platform_Trait_NeedsBrightnessAdjustment, TEXT("Platform does not require brightness adjustment.")));

			Graphics->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			ULyraSettingAction_SafeZoneEditor* Setting = NewObject<ULyraSettingAction_SafeZoneEditor>();
			Setting->SetDevName(TEXT("SafeZone"));
			Setting->SetDisplayName(LOCTEXT("SafeZone_Name", "Safe Zone"));
			Setting->SetDescriptionRichText(LOCTEXT("SafeZone_Description", "Set the UI safe zone for the platform."));
			Setting->SetActionText(LOCTEXT("SafeZone_Action", "Set Safe Zone"));
			Setting->SetNamedAction(GameSettings_Action_EditSafeZone);

			Setting->AddEditCondition(FWhenPlayingAsPrimaryPlayer::Get());
			Setting->AddEditCondition(MakeShared<FWhenCondition>([](const ULocalPlayer*, FGameSettingEditableState& InOutEditState) {
				FDisplayMetrics Metrics;
				FSlateApplication::Get().GetCachedDisplayMetrics(Metrics);
				if (Metrics.TitleSafePaddingSize.Size() == 0)
				{
					InOutEditState.Kill(TEXT("Platform does not have any TitleSafePaddingSize configured in the display metrics."));
				}
			}));

			Graphics->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
	}

	// Graphics Optimization
	////////////////////////////////////////////////////////////////////////////////////
	// {
	// 	UGameSettingCollection* GraphicsOptimization = NewObject<UGameSettingCollection>();
	// 	GraphicsOptimization->SetDevName(TEXT("GraphicsOptimization"));
	// 	GraphicsOptimization->SetDisplayName(LOCTEXT("GraphicsCollection_Name", "Graphics Optimization"));
	// 	Screen->AddSetting(GraphicsOptimization);
	//
	// 	//----------------------------------------------------------------------------------
	// 	{
	// 		UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
	// 		Setting->SetDevName(TEXT("NVDIA DLSS")); //NVDIA DLSS, ColorBlindMode
	// 		Setting->SetDisplayName(LOCTEXT("DLSS_Mode_Name", "NVDIA DLSS"));
	// 		Setting->SetDescriptionRichText(LOCTEXT("ColorBlindMode_Description", "DLSS 3.5, will auto scale your resolution with the use of super sampling and AI techniques to give better performance at a cost of minimalistic visual difference."));
	//
	// 		// r.Streamline.DLSSG.Enable 0 or 1
	// 		Setting->SetDynamicGetter();
	// 		Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetDLSSMode));
	// 		
	//
	// 		// Setting->SetDefaultValue(GetDefault<ULyraSettingsShared>()->GetDLSSMode());
	// 		
	// 		Setting->AddOption(0, LOCTEXT("DLSSModeSettingOff", "Off"));
	// 		Setting->AddOption(1, LOCTEXT("DLSSModeSettingQuality", "Quality"));
	// 		Setting->AddOption(2, LOCTEXT("DLSSModeSettingBalanced", "Balanced"));
	// 		Setting->AddOption(3, LOCTEXT("DLSSModeSettingUltraPerformance", "Ultra Performance"));
	//
	// 		Setting->AddEditCondition(FWhenPlayingAsPrimaryPlayer::Get());
	//
	// 		GraphicsOptimization->AddSetting(Setting);
	// 	}
	// 	//----------------------------------------------------------------------------------
	// }
	
	// Graphics Quality
	////////////////////////////////////////////////////////////////////////////////////
	{
		UGameSettingCollection* GraphicsQuality = NewObject<UGameSettingCollection>();
		GraphicsQuality->SetDevName(TEXT("GraphicsQuality"));
		GraphicsQuality->SetDisplayName(LOCTEXT("GraphicsQuality_Name", "Graphics Quality"));
		Screen->AddSetting(GraphicsQuality);

		UGameSetting* AutoSetQuality = nullptr;
		UGameSetting* GraphicsQualityPresets = nullptr;

		//----------------------------------------------------------------------------------
		{
			// Console-style device profile selection
			UGameSettingValueDiscreteDynamic* Setting = NewObject<UGameSettingValueDiscreteDynamic>();
			Setting->SetDevName(TEXT("DeviceProfileSuffix"));
			Setting->SetDisplayName(LOCTEXT("DeviceProfileSuffix_Name", "Quality Presets"));
			Setting->SetDescriptionRichText(LOCTEXT("DeviceProfileSuffix_Description", "Choose between different quality presets to make a trade off between quality and speed."));
			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetDesiredDeviceProfileQualitySuffix));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetDesiredDeviceProfileQualitySuffix));

			const ULyraPlatformSpecificRenderingSettings* PlatformSettings = ULyraPlatformSpecificRenderingSettings::Get();

			Setting->SetDefaultValueFromString(PlatformSettings->DefaultDeviceProfileSuffix);
			for (const FLyraQualityDeviceProfileVariant& Variant : PlatformSettings->UserFacingDeviceProfileOptions)
			{
				if (FPlatformMisc::GetMaxRefreshRate() >= Variant.MinRefreshRate)
				{
					Setting->AddDynamicOption(Variant.DeviceProfileSuffix, Variant.DisplayName);
				}
			}

			if (Setting->GetDynamicOptions().Num() > 1)
			{
				GraphicsQuality->AddSetting(Setting);
			}
		}

		//----------------------------------------------------------------------------------
		{
			// Mobile style frame rate selection
			ULyraSettingValueDiscrete_MobileFPSType* Setting = NewObject<ULyraSettingValueDiscrete_MobileFPSType>();
			MobileFPSType = Setting;

			Setting->SetDevName(TEXT("FrameRateLimit_Mobile"));
			Setting->SetDisplayName(LOCTEXT("FrameRateLimit_Mobile_Name", "Frame Rate Limit"));
			Setting->SetDescriptionRichText(LOCTEXT("FrameRateLimit_Mobile_Description", "Select a desired framerate. Use this to fine tune performance on your device."));

			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_FramePacingMode>(ELyraFramePacingMode::MobileStyle));

			GraphicsQuality->AddSetting(Setting);
		}

		//----------------------------------------------------------------------------------
		{
			UGameSettingAction* Setting = NewObject<UGameSettingAction>();
			Setting->SetDevName(TEXT("AutoSetQuality"));
			Setting->SetDisplayName(LOCTEXT("AutoSetQuality_Name", "Auto-Set Quality"));
			Setting->SetDescriptionRichText(LOCTEXT("AutoSetQuality_Description", "Automatically configure the graphics quality options based on a benchmark of the hardware."));

			Setting->SetDoesActionDirtySettings(true);
			Setting->SetActionText(LOCTEXT("AutoSetQuality_Action", "Auto-Set"));
			Setting->SetCustomAction([](ULocalPlayer* LocalPlayer)
			{
				const ULyraPlatformSpecificRenderingSettings* PlatformSettings = ULyraPlatformSpecificRenderingSettings::Get();
				if (PlatformSettings->FramePacingMode == ELyraFramePacingMode::MobileStyle)
				{
					ULyraSettingsLocal::Get()->ResetToMobileDeviceDefaults();
				}
				else
				{
					const ULyraLocalPlayer* LyraLocalPlayer = CastChecked<ULyraLocalPlayer>(LocalPlayer);
					// We don't save state until users apply the settings.
					constexpr bool bImmediatelySaveState = false;
					LyraLocalPlayer->GetLocalSettings()->RunAutoBenchmark(bImmediatelySaveState);
				}
			});

			Setting->AddEditCondition(MakeShared<FWhenCondition>([](const ULocalPlayer* LocalPlayer, FGameSettingEditableState& InOutEditState)
			{
				const ULyraPlatformSpecificRenderingSettings* PlatformSettings = ULyraPlatformSpecificRenderingSettings::Get();
				const bool bCanUseDueToMobile = (PlatformSettings->FramePacingMode == ELyraFramePacingMode::MobileStyle);

				const ULyraLocalPlayer* LyraLocalPlayer = CastChecked<ULyraLocalPlayer>(LocalPlayer);
				const bool bCanBenchmark = LyraLocalPlayer->GetLocalSettings()->CanRunAutoBenchmark();

				if (!bCanUseDueToMobile && !bCanBenchmark)
				{
					InOutEditState.Kill(TEXT("Auto quality not supported"));
				}
			}));

			if (MobileFPSType != nullptr)
			{
				MobileFPSType->AddEditDependency(Setting);
			}

			GraphicsQuality->AddSetting(Setting);

			AutoSetQuality = Setting;
		}
		//----------------------------------------------------------------------------------
		{
			ULyraSettingValueDiscrete_OverallQuality* Setting = NewObject<ULyraSettingValueDiscrete_OverallQuality>();
			Setting->SetDevName(TEXT("GraphicsQualityPresets"));
			Setting->SetDisplayName(LOCTEXT("GraphicsQualityPresets_Name", "Quality Presets"));
			Setting->SetDescriptionRichText(LOCTEXT("GraphicsQualityPresets_Description", "Quality Preset allows you to adjust multiple video options at once. Try a few options to see what fits your preference and device's performance."));

			Setting->AddEditDependency(AutoSetQuality);

			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_FramePacingMode>(ELyraFramePacingMode::ConsoleStyle, EFramePacingEditCondition::DisableIf));

			if (MobileFPSType != nullptr)
			{
				Setting->AddEditDependency(MobileFPSType);
				MobileFPSType->AddEditDependency(Setting);
			}

			GraphicsQuality->AddSetting(Setting);

			GraphicsQualityPresets = Setting;
		}
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueScalarDynamic* Setting = NewObject<UGameSettingValueScalarDynamic>();
			Setting->SetDevName(TEXT("ResolutionScale"));
			Setting->SetDisplayName(LOCTEXT("ResolutionScale_Name", "3D Resolution"));
			Setting->SetDescriptionRichText(LOCTEXT("ResolutionScale_Description", "3D resolution determines the resolution that objects are rendered in game, but does not affect the main menu.  Lower resolutions can significantly increase frame rate."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetResolutionScaleNormalized));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetResolutionScaleNormalized));
			Setting->SetDisplayFormat(UGameSettingValueScalarDynamic::ZeroToOnePercent);

			Setting->AddEditDependency(AutoSetQuality);
			Setting->AddEditDependency(GraphicsQualityPresets);
			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_VideoQuality>(TEXT("Platform does not support 3D Resolution")));
			//@TODO: Add support for 3d res on mobile

			// When this setting changes, it can GraphicsQualityPresets to be set to custom, or a particular preset.
			GraphicsQualityPresets->AddEditDependency(Setting);
			GraphicsQuality->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
			Setting->SetDevName(TEXT("GlobalIlluminationQuality"));
			Setting->SetDisplayName(LOCTEXT("GlobalIlluminationQuality_Name", "Global Illumination"));
			Setting->SetDescriptionRichText(LOCTEXT("GlobalIlluminationQuality_Description", "Global Illumination controls the quality of dynamically calculated indirect lighting bounces, sky shadowing and Ambient Occlusion. Settings of 'High' and above use more accurate ray tracing methods to solve lighting, but can reduce performance."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetGlobalIlluminationQuality));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetGlobalIlluminationQuality));
			Setting->AddOption(0, LOCTEXT("VisualEffectQualityLow", "Low"));
			Setting->AddOption(1, LOCTEXT("VisualEffectQualityMedium", "Medium"));
			Setting->AddOption(2, LOCTEXT("VisualEffectQualityHigh", "High"));
			Setting->AddOption(3, LOCTEXT("VisualEffectQualityEpic", "Epic"));

			Setting->AddEditDependency(AutoSetQuality);
			Setting->AddEditDependency(GraphicsQualityPresets);
			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_VideoQuality>(TEXT("Platform does not support GlobalIlluminationQuality")));

			// When this setting changes, it can GraphicsQualityPresets to be set to custom, or a particular preset.
			GraphicsQualityPresets->AddEditDependency(Setting);

			GraphicsQuality->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
			Setting->SetDevName(TEXT("Shadows"));
			Setting->SetDisplayName(LOCTEXT("Shadows_Name", "Shadows"));
			Setting->SetDescriptionRichText(LOCTEXT("Shadows_Description", "Shadow quality determines the resolution and view distance of dynamic shadows. Shadows improve visual quality and give better depth perception, but can reduce performance."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetShadowQuality));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetShadowQuality));
			Setting->AddOption(0, LOCTEXT("ShadowLow", "Off"));
			Setting->AddOption(1, LOCTEXT("ShadowMedium", "Medium"));
			Setting->AddOption(2, LOCTEXT("ShadowHigh", "High"));
			Setting->AddOption(3, LOCTEXT("ShadowEpic", "Epic"));

			Setting->AddEditDependency(AutoSetQuality);
			Setting->AddEditDependency(GraphicsQualityPresets);
			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_VideoQuality>(TEXT("Platform does not support Shadows")));

			// When this setting changes, it can GraphicsQualityPresets to be set to custom, or a particular preset.
			GraphicsQualityPresets->AddEditDependency(Setting);

			GraphicsQuality->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
			Setting->SetDevName(TEXT("AntiAliasing"));
			Setting->SetDisplayName(LOCTEXT("AntiAliasing_Name", "Anti-Aliasing"));
			Setting->SetDescriptionRichText(LOCTEXT("AntiAliasing_Description", "Anti-Aliasing reduces jaggy artifacts along geometry edges. Increasing this setting will make edges look smoother, but can reduce performance. Higher settings mean more anti-aliasing."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetAntiAliasingQuality));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetAntiAliasingQuality));
			Setting->AddOption(0, LOCTEXT("AntiAliasingLow", "Off"));
			Setting->AddOption(1, LOCTEXT("AntiAliasingMedium", "Medium"));
			Setting->AddOption(2, LOCTEXT("AntiAliasingHigh", "High"));
			Setting->AddOption(3, LOCTEXT("AntiAliasingEpic", "Epic"));

			Setting->AddEditDependency(AutoSetQuality);
			Setting->AddEditDependency(GraphicsQualityPresets);
			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_VideoQuality>(TEXT("Platform does not support Anti-Aliasing")));

			// When this setting changes, it can GraphicsQualityPresets to be set to custom, or a particular preset.
			GraphicsQualityPresets->AddEditDependency(Setting);

			GraphicsQuality->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
			Setting->SetDevName(TEXT("ViewDistance"));
			Setting->SetDisplayName(LOCTEXT("ViewDistance_Name", "View Distance"));
			Setting->SetDescriptionRichText(LOCTEXT("ViewDistance_Description", "View distance determines how far away objects are culled for performance."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetViewDistanceQuality));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetViewDistanceQuality));
			Setting->AddOption(0, LOCTEXT("ViewDistanceNear", "Near"));
			Setting->AddOption(1, LOCTEXT("ViewDistanceMedium", "Medium"));
			Setting->AddOption(2, LOCTEXT("ViewDistanceFar", "Far"));
			Setting->AddOption(3, LOCTEXT("ViewDistanceEpic", "Epic"));

			Setting->AddEditDependency(AutoSetQuality);
			Setting->AddEditDependency(GraphicsQualityPresets);
			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_VideoQuality>(TEXT("Platform does not support View Distance")));

			// When this setting changes, it can GraphicsQualityPresets to be set to custom, or a particular preset.
			GraphicsQualityPresets->AddEditDependency(Setting);

			GraphicsQuality->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
			Setting->SetDevName(TEXT("TextureQuality"));
			Setting->SetDisplayName(LOCTEXT("TextureQuality_Name", "Textures"));

			Setting->SetDescriptionRichText(LOCTEXT("TextureQuality_Description", "Texture quality determines the resolution of textures in game. Increasing this setting will make objects more detailed, but can reduce performance."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetTextureQuality));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetTextureQuality));
			Setting->AddOption(0, LOCTEXT("TextureQualityLow", "Low"));
			Setting->AddOption(1, LOCTEXT("TextureQualityMedium", "Medium"));
			Setting->AddOption(2, LOCTEXT("TextureQualityHigh", "High"));
			Setting->AddOption(3, LOCTEXT("TextureQualityEpic", "Epic"));

			Setting->AddEditDependency(AutoSetQuality);
			Setting->AddEditDependency(GraphicsQualityPresets);
			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_VideoQuality>(TEXT("Platform does not support Texture quality")));

			// When this setting changes, it can GraphicsQualityPresets to be set to custom, or a particular preset.
			GraphicsQualityPresets->AddEditDependency(Setting);

			GraphicsQuality->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
			Setting->SetDevName(TEXT("VisualEffectQuality"));
			Setting->SetDisplayName(LOCTEXT("VisualEffectQuality_Name", "Effects"));
			Setting->SetDescriptionRichText(LOCTEXT("VisualEffectQuality_Description", "Effects determines the quality of visual effects and lighting in game. Increasing this setting will increase the quality of visual effects, but can reduce performance."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetVisualEffectQuality));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetVisualEffectQuality));
			Setting->AddOption(0, LOCTEXT("VisualEffectQualityLow", "Low"));
			Setting->AddOption(1, LOCTEXT("VisualEffectQualityMedium", "Medium"));
			Setting->AddOption(2, LOCTEXT("VisualEffectQualityHigh", "High"));
			Setting->AddOption(3, LOCTEXT("VisualEffectQualityEpic", "Epic"));

			Setting->AddEditDependency(AutoSetQuality);
			Setting->AddEditDependency(GraphicsQualityPresets);
			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_VideoQuality>(TEXT("Platform does not support VisualEffectQuality")));

			// When this setting changes, it can GraphicsQualityPresets to be set to custom, or a particular preset.
			GraphicsQualityPresets->AddEditDependency(Setting);

			GraphicsQuality->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
			Setting->SetDevName(TEXT("ReflectionQuality"));
			Setting->SetDisplayName(LOCTEXT("ReflectionQuality_Name", "Reflections"));
			Setting->SetDescriptionRichText(LOCTEXT("ReflectionQuality_Description", "Reflection quality determines the resolution and accuracy of reflections.  Settings of 'High' and above use more accurate ray tracing methods to solve reflections, but can reduce performance."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetReflectionQuality));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetReflectionQuality));
			Setting->AddOption(0, LOCTEXT("VisualEffectQualityLow", "Low"));
			Setting->AddOption(1, LOCTEXT("VisualEffectQualityMedium", "Medium"));
			Setting->AddOption(2, LOCTEXT("VisualEffectQualityHigh", "High"));
			Setting->AddOption(3, LOCTEXT("VisualEffectQualityEpic", "Epic"));

			Setting->AddEditDependency(AutoSetQuality);
			Setting->AddEditDependency(GraphicsQualityPresets);
			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_VideoQuality>(TEXT("Platform does not support ReflectionQuality")));

			// When this setting changes, it can GraphicsQualityPresets to be set to custom, or a particular preset.
			GraphicsQualityPresets->AddEditDependency(Setting);

			GraphicsQuality->AddSetting(Setting);
		}
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
			Setting->SetDevName(TEXT("PostProcessingQuality"));
			Setting->SetDisplayName(LOCTEXT("PostProcessingQuality_Name", "Post Processing"));
			Setting->SetDescriptionRichText(LOCTEXT("PostProcessingQuality_Description", "Post Processing effects include Motion Blur, Depth of Field and Bloom. Increasing this setting improves the quality of post process effects, but can reduce performance."));  

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetPostProcessingQuality));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetPostProcessingQuality));
			Setting->AddOption(0, LOCTEXT("PostProcessingQualityLow", "Low"));
			Setting->AddOption(1, LOCTEXT("PostProcessingQualityMedium", "Medium"));
			Setting->AddOption(2, LOCTEXT("PostProcessingQualityHigh", "High"));
			Setting->AddOption(3, LOCTEXT("PostProcessingQualityEpic", "Epic"));

			Setting->AddEditDependency(AutoSetQuality);
			Setting->AddEditDependency(GraphicsQualityPresets);
			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_VideoQuality>(TEXT("Platform does not support PostProcessingQuality")));

			// When this setting changes, it can GraphicsQualityPresets to be set to custom, or a particular preset.
			GraphicsQualityPresets->AddEditDependency(Setting);

			GraphicsQuality->AddSetting(Setting);
		}

	}

	// NVIDIA
	////////////////////////////////////////////////////////////////////////////////////

	{
		UGameSettingCollection* NvidiaGraphics = NewObject<UGameSettingCollection>();
		NvidiaGraphics->SetDevName(TEXT("Nvidia DLSS"));
		NvidiaGraphics->SetDisplayName(LOCTEXT("NvidiaDLSS_Name", "Nvidia DLSS"));
		Screen->AddSetting(NvidiaGraphics);

		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Enum* Setting = NewObject<UGameSettingValueDiscreteDynamic_Enum>();
			Setting->SetDevName(TEXT("NvidiaDLSSMode"));
			Setting->SetDisplayName(LOCTEXT("NvidiaDLSSMode_Name", "Nvidia DLSS Mode"));
			Setting->SetDescriptionRichText(LOCTEXT("NvidiaDLSSMode_Description", "Select the Nvidia DLSS Mode."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetNvidiaDLSSMode));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetNvidiaDLSSMode));
			Setting->SetDefaultValue(GetDefault<ULyraSettingsLocal>()->GetNvidiaDLSSMode());
			Setting->AddEnumOption(ENvidiaDLSSMode::Off, LOCTEXT("NvidiaDLSS_Off", "Off"));
			Setting->AddEnumOption(ENvidiaDLSSMode::DLAA, LOCTEXT("NvidiaDLSS_DLAA", "DLAA"));
			Setting->AddEnumOption(ENvidiaDLSSMode::Quality, LOCTEXT("NvidiaDLSS_Quality", "Quality"));
			Setting->AddEnumOption(ENvidiaDLSSMode::Balanced, LOCTEXT("NvidiaDLSS_Balanced", "Balanced"));
			Setting->AddEnumOption(ENvidiaDLSSMode::Performance, LOCTEXT("NvidiaDLSS_Performance", "Performance"));
			Setting->AddEnumOption(ENvidiaDLSSMode::Ultra_Performance, LOCTEXT("NvidiaDLSS_Ultra_Performance", "Ultra Performance"));
			Setting->AddEnumOption(ENvidiaDLSSMode::Auto, LOCTEXT("NvidiaDLSS_Auto", "Auto"));

			Setting->AddEditCondition(FWhenPlayingAsPrimaryPlayer::Get());

			NvidiaDLSSModeDependency = Setting;

			Setting->AddEditCondition(MakeShared<FWhenCondition>([](const ULocalPlayer*, FGameSettingEditableState& InOutEditState) {
				const bool bDLSSSupported = UDLSSLibrary::IsDLSSSupported();

				if (!bDLSSSupported)
				{
					InOutEditState.Disable(LOCTEXT("NvidiaDLSS", "Nvidia DLSS has to be supported."));
				}
			}));

			NvidiaGraphics->AddSetting(Setting);
		}

		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
			Setting->SetDevName(TEXT("NvidiaDLSSSharpness"));
			Setting->SetDisplayName(LOCTEXT("NvidiaDLSSSharpness_Name", "Nvidia DLSS Sharpness"));
			Setting->SetDescriptionRichText(LOCTEXT("NvidiaDLSSSharpness_Description", "Nvidia DLSS Sharpness value 0-10."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetNvidiaDLSSSharpness));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetNvidiaDLSSSharpness));
			Setting->SetDefaultValue(GetDefault<ULyraSettingsLocal>()->GetNvidiaDLSSSharpness());
			for (int32 Index = 0; Index <= 10; Index++)
			{
				Setting->AddOption(Index, FText::AsNumber(Index));
			}

			Setting->AddEditCondition(FWhenPlayingAsPrimaryPlayer::Get());

			Setting->AddEditDependency(NvidiaDLSSModeDependency);
			Setting->AddEditCondition(MakeShared<FWhenCondition>([NvidiaDLSSModeDependency](const ULocalPlayer*, FGameSettingEditableState& InOutEditState) {
				const bool bDLSSModeOff = NvidiaDLSSModeDependency->GetValue<ENvidiaDLSSMode>() == ENvidiaDLSSMode::Off;
				const bool bDLSSSupported = UDLSSLibrary::IsDLSSSupported();

				if (!bDLSSSupported || bDLSSModeOff)
				{
					InOutEditState.Disable(LOCTEXT("NvidiaDLSS", "Nvidia DLSS has to be supported and not off."));
				}
			}));

			/*
			Setting->AddEditDependency(NvidiaDLSSModeDependency);
			Setting->AddEditCondition(MakeShared<FWhenCondition>([this, WindowModeSetting, NvidiaDLSSModeDependency](const ULocalPlayer*, FGameSettingEditableState& InOutEditState) {
				if (GEngine)
				{
					if (const UWorld* World = GEngine->GetCurrentPlayWorld())
					{
						if (World->GetGameInstance()->GetClass()->ImplementsInterface(UNvidiaSettingsManagerInterface::StaticClass()))
						{
							const bool bDLSSModeOff = NvidiaDLSSModeDependency->GetValue<ENvidiaDLSSMode>() == ENvidiaDLSSMode::Off;
							const bool bDLSSSupported = INvidiaSettingsManagerInterface::Execute_GetDLSSSupported(World->GetGameInstance());

							if (!bDLSSSupported || bDLSSModeOff)
							{
								InOutEditState.Disable(LOCTEXT("NvidiaDLSS", "Nvidia DLSS has to be supported and not off."));
							}
						}
					}
				}
			}));
			*/

			NvidiaGraphics->AddSetting(Setting);
		}

		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Bool* Setting = NewObject<UGameSettingValueDiscreteDynamic_Bool>();
			Setting->SetDevName(TEXT("NvidiaDLSSFrameGeneration"));
			Setting->SetDisplayName(LOCTEXT("NvidiaDLSSFrameGeneration_Name", "Nvidia DLSS Frame Generation"));
			Setting->SetDescriptionRichText(LOCTEXT("NvidiaDLSSFrameGeneration_Description", "Whether to enable Nvidia DLSS Frame Generation."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetNvidiaDLSSFrameGenerationEnabled));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetNvidiaDLSSFrameGenerationEnabled));
			Setting->SetDefaultValue(GetDefault<ULyraSettingsLocal>()->GetNvidiaDLSSFrameGenerationEnabled());

			Setting->AddEditCondition(FWhenPlayingAsPrimaryPlayer::Get());

			Setting->AddEditDependency(NvidiaDLSSModeDependency);
			Setting->AddEditCondition(MakeShared<FWhenCondition>([NvidiaDLSSModeDependency](const ULocalPlayer*, FGameSettingEditableState& InOutEditState) {
				const bool bDLSSModeOff = NvidiaDLSSModeDependency->GetValue<ENvidiaDLSSMode>() == ENvidiaDLSSMode::Off;
				const bool bDLSSSupported = UDLSSLibrary::IsDLSSSupported();
				const bool bDLSSHardwareCompatible = UDLSSLibrary::QueryDLSSSupport() != UDLSSSupport::NotSupportedIncompatibleHardware;

				if (!bDLSSSupported || !bDLSSHardwareCompatible || bDLSSModeOff)
				{
					InOutEditState.Disable(LOCTEXT("NvidiaDLSS", "Nvidia DLSS has to be supported, hardware compatible and not off."));
				}
			}));

			NvidiaGraphics->AddSetting(Setting);
		}

		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Enum* Setting = NewObject<UGameSettingValueDiscreteDynamic_Enum>();
			Setting->SetDevName(TEXT("NvidiaReflex"));
			Setting->SetDisplayName(LOCTEXT("NvidiaReflex_Name", "Nvidia Reflex"));
			Setting->SetDescriptionRichText(LOCTEXT("NvidiaReflex_Description", "Select the Nvidia Reflex."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetNvidiaReflex));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetNvidiaReflex));
			Setting->SetDefaultValue(GetDefault<ULyraSettingsLocal>()->GetNvidiaReflex());
			Setting->AddEnumOption(ENvidiaReflex::Disabled, LOCTEXT("NvidiaReflex_Disabled", "Disabled"));
			Setting->AddEnumOption(ENvidiaReflex::Enabled, LOCTEXT("NvidiaReflex_Enabled", "Enabled"));
			Setting->AddEnumOption(ENvidiaReflex::Enabled_Boost, LOCTEXT("NvidiaReflex_Enabled_Boost", "Enabled Boost"));

			Setting->AddEditCondition(FWhenPlayingAsPrimaryPlayer::Get());

			Setting->AddEditDependency(NvidiaDLSSModeDependency);
			Setting->AddEditCondition(MakeShared<FWhenCondition>([NvidiaDLSSModeDependency](const ULocalPlayer*, FGameSettingEditableState& InOutEditState) {
				const bool bDLSSModeOff = NvidiaDLSSModeDependency->GetValue<ENvidiaDLSSMode>() == ENvidiaDLSSMode::Off;
				const bool bDLSSSupported = UDLSSLibrary::IsDLSSSupported();

				if (!bDLSSSupported || bDLSSModeOff)
				{
					InOutEditState.Disable(LOCTEXT("NvidiaDLSS", "Nvidia DLSS has to be supported and not off."));
				}
			}));

			NvidiaGraphics->AddSetting(Setting);
		}

		/*
		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Bool* Setting = NewObject<UGameSettingValueDiscreteDynamic_Bool>();
			Setting->SetDevName(TEXT("RayTracing"));
			Setting->SetDisplayName(LOCTEXT("RayTracing_Name", "Ray Tracing"));
			Setting->SetDescriptionRichText(LOCTEXT("RayTracing_Description", "Whether to enable Ray Tracing."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetIsRayTracingEnabled));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetRayTracingEnabled));
			Setting->SetDefaultValue(GetDefault<ULyraSettingsLocal>()->GetIsRayTracingEnabled());

			Setting->AddEditCondition(FWhenPlayingAsPrimaryPlayer::Get());

			NvidiaGraphics->AddSetting(Setting);
		}

		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Enum* Setting = NewObject<UGameSettingValueDiscreteDynamic_Enum>();
			Setting->SetDevName(TEXT("UpscalingMode"));
			Setting->SetDisplayName(LOCTEXT("UpscalingMode_Name", "Upscaling Mode"));
			Setting->SetDescriptionRichText(LOCTEXT("UpscalingMode_Description", "Select the upscaling mode, by default is the Built-In, you get to additionally choose Nvidia DLSS or Nvidia Image Scaling."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetNvidiaUpscaling));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetNvidiaUpscaling));
			Setting->SetDefaultValue(GetDefault<ULyraSettingsLocal>()->GetNvidiaUpscaling());
			Setting->AddEnumOption(EVideoUpscalingMode::BuiltIn, LOCTEXT("VideoUpscalingBuiltIn", "Built-In"));
			Setting->AddEnumOption(EVideoUpscalingMode::NvidiaDLSS, LOCTEXT("VideoUpscalingNvidiaDLSS", "Nvidia DLSS"));
			Setting->AddEnumOption(EVideoUpscalingMode::NvidiaImageSampling, LOCTEXT("VideoUpscalingNvidiaImageSampling", "Nvidia Image Sampling"));

			Setting->AddEditCondition(FWhenPlayingAsPrimaryPlayer::Get());

			NvidiaGraphics->AddSetting(Setting);
		}
		*/
	}

	// Advanced Graphics
	////////////////////////////////////////////////////////////////////////////////////
	{
		UGameSettingCollection* AdvancedGraphics = NewObject<UGameSettingCollection>();
		AdvancedGraphics->SetDevName(TEXT("AdvancedGraphics"));
		AdvancedGraphics->SetDisplayName(LOCTEXT("AdvancedGraphics_Name", "Advanced Graphics"));
		Screen->AddSetting(AdvancedGraphics);

		//----------------------------------------------------------------------------------
		{
			UGameSettingValueDiscreteDynamic_Bool* Setting = NewObject<UGameSettingValueDiscreteDynamic_Bool>();
			Setting->SetDevName(TEXT("VerticalSync"));
			Setting->SetDisplayName(LOCTEXT("VerticalSync_Name", "Vertical Sync"));
			Setting->SetDescriptionRichText(LOCTEXT("VerticalSync_Description", "Enabling Vertical Sync eliminates screen tearing by always rendering and presenting a full frame. Disabling Vertical Sync can give higher frame rate and better input response, but can result in horizontal screen tearing."));

			Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(IsVSyncEnabled));
			Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetVSyncEnabled));
			Setting->SetDefaultValue(false);

			Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_FramePacingMode>(ELyraFramePacingMode::DesktopStyle));

			Setting->AddEditDependency(WindowModeSetting);
			Setting->AddEditCondition(MakeShared<FWhenCondition>([WindowModeSetting](const ULocalPlayer*, FGameSettingEditableState& InOutEditState) {
				if (WindowModeSetting->GetValue<EWindowMode::Type>() != EWindowMode::Fullscreen)
				{
					InOutEditState.Disable(LOCTEXT("FullscreenNeededForVSync", "This feature only works if 'Window Mode' is set to 'Fullscreen'."));
				}
			}));

			AdvancedGraphics->AddSetting(Setting);
		}
	}

	return Screen;
}

void AddFrameRateOptions(UGameSettingValueDiscreteDynamic_Number* Setting)
{
	const FText FPSFormat = LOCTEXT("FPSFormat", "{0} FPS");
	for (int32 Rate : GetDefault<ULyraPerformanceSettings>()->DesktopFrameRateLimits)
	{
		Setting->AddOption((float)Rate, FText::Format(FPSFormat, Rate));
	}
	Setting->AddOption(0.0f, LOCTEXT("UnlimitedFPS", "Unlimited"));
}

void ULyraGameSettingRegistry::InitializeVideoSettings_FrameRates(UGameSettingCollection* Screen, ULyraLocalPlayer* InLocalPlayer)
{
	//----------------------------------------------------------------------------------
	{
		UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
		Setting->SetDevName(TEXT("FrameRateLimit_OnBattery"));
		Setting->SetDisplayName(LOCTEXT("FrameRateLimit_OnBattery_Name", "Frame Rate Limit (On Battery)"));
		Setting->SetDescriptionRichText(LOCTEXT("FrameRateLimit_OnBattery_Description", "Frame rate limit when running on battery. Set this lower for a more consistent frame rate or higher for the best experience on faster machines. You may need to disable Vsync to reach high frame rates."));

		Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetFrameRateLimit_OnBattery));
		Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetFrameRateLimit_OnBattery));
		Setting->SetDefaultValue(GetDefault<ULyraSettingsLocal>()->GetFrameRateLimit_OnBattery());

		Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_FramePacingMode>(ELyraFramePacingMode::DesktopStyle));
		//@TODO: Hide if this device doesn't have a battery (no API for this right now)

		AddFrameRateOptions(Setting);

		Screen->AddSetting(Setting);
	}
	//----------------------------------------------------------------------------------
	{
		UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
		Setting->SetDevName(TEXT("FrameRateLimit_InMenu"));
		Setting->SetDisplayName(LOCTEXT("FrameRateLimit_InMenu_Name", "Frame Rate Limit (Menu)"));
		Setting->SetDescriptionRichText(LOCTEXT("FrameRateLimit_InMenu_Description", "Frame rate limit when in the menu. Set this lower for a more consistent frame rate or higher for the best experience on faster machines. You may need to disable Vsync to reach high frame rates."));

		Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetFrameRateLimit_InMenu));
		Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetFrameRateLimit_InMenu));
		Setting->SetDefaultValue(GetDefault<ULyraSettingsLocal>()->GetFrameRateLimit_InMenu());
		Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_FramePacingMode>(ELyraFramePacingMode::DesktopStyle));

		AddFrameRateOptions(Setting);

		Screen->AddSetting(Setting);
	}
	//----------------------------------------------------------------------------------
	{
		UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
		Setting->SetDevName(TEXT("FrameRateLimit_WhenBackgrounded"));
		Setting->SetDisplayName(LOCTEXT("FrameRateLimit_WhenBackgrounded_Name", "Frame Rate Limit (Background)"));
		Setting->SetDescriptionRichText(LOCTEXT("FrameRateLimit_WhenBackgrounded_Description", "Frame rate limit when in the background. Set this lower for a more consistent frame rate or higher for the best experience on faster machines. You may need to disable Vsync to reach high frame rates."));

		Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetFrameRateLimit_WhenBackgrounded));
		Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetFrameRateLimit_WhenBackgrounded));
		Setting->SetDefaultValue(GetDefault<ULyraSettingsLocal>()->GetFrameRateLimit_WhenBackgrounded());
		Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_FramePacingMode>(ELyraFramePacingMode::DesktopStyle));

		AddFrameRateOptions(Setting);

		Screen->AddSetting(Setting);
	}
	//----------------------------------------------------------------------------------
	{
		UGameSettingValueDiscreteDynamic_Number* Setting = NewObject<UGameSettingValueDiscreteDynamic_Number>();
		Setting->SetDevName(TEXT("FrameRateLimit_Always"));
		Setting->SetDisplayName(LOCTEXT("FrameRateLimit_Always_Name", "Frame Rate Limit"));
		Setting->SetDescriptionRichText(LOCTEXT("FrameRateLimit_Always_Description", "Frame rate limit sets the highest frame rate that is allowed. Set this lower for a more consistent frame rate or higher for the best experience on faster machines. You may need to disable Vsync to reach high frame rates."));

		Setting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetFrameRateLimit_Always));
		Setting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetFrameRateLimit_Always));
		Setting->SetDefaultValue(GetDefault<ULyraSettingsLocal>()->GetFrameRateLimit_Always());
		Setting->AddEditCondition(MakeShared<FGameSettingEditCondition_FramePacingMode>(ELyraFramePacingMode::DesktopStyle));

		AddFrameRateOptions(Setting);

		Screen->AddSetting(Setting);
	}
}

#undef LOCTEXT_NAMESPACE
