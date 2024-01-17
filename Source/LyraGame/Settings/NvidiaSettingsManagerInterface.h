// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "NvidiaSettingsManagerInterface.generated.h"

/** */
UINTERFACE(Blueprintable)
class UNvidiaSettingsManagerInterface : public UInterface
{
	GENERATED_BODY()
};

class INvidiaSettingsManagerInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Nvidia DLSS")
	void SetNvidiaDLSSMode(ENvidiaDLSSMode InNvidiaDLSSMode);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Nvidia DLSS")
	void SetNvidiaDLSSSharpness(int32 InNvidiaDLSSSharpness);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Nvidia DLSS")
	void SetNvidiaDLSSFrameGeneration(bool bInEnable);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Nvidia DLSS")
	void SetNvidiaReflex(ENvidiaReflex InNvidiaReflex);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Nvidia DLSS")
	bool GetDLSSSupported();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Nvidia DLSS")
	bool GetDLSSHardwareCompatible();

};
