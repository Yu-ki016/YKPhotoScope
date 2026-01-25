// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PhotoScopeBlueprintLibrary.generated.h"

enum ETextureRenderTargetFormat : int;
class UGridPanel;
/**
 * 
 */
UCLASS()
class YKPHOTOSCOPE_API UPhotoScopeBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "Photo Scope", meta=(WorldContext = "WorldContextObject"))
	static void SetGridPanelLayout(UObject* WorldContextObject, UGridPanel* GridPanel, TSubclassOf<UUserWidget> ContentClass, FIntPoint Layout);
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope", meta = (WorldContext = "WorldContextObject"))
	static UTextureRenderTarget2D* UpdateOrCreateRenderTarget(
		const UObject* WorldContextObject,
		UTextureRenderTarget2D* TargetRT,
		int32 Width,
		int32 Height,
		ETextureRenderTargetFormat Format = RTF_RGBA16f,
		bool bSRGB = false,
		bool bSupportUAVs = true,
		FLinearColor ClearColor = FLinearColor::Black,
		bool bAllowDynamicResizing = true);
	
	UFUNCTION(BlueprintCallable, Category = "PhotoScope")
	static void ForceRedrawEditor();
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	static  FIntPoint GetActiveViewportBufferSize();
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	static UMaterialInstanceDynamic* GetOrCreateTransientDMI(UMaterialInstanceDynamic* InDMI, FName InDMIName, UMaterialInterface* InMaterialInterface);
	
	static UMaterialInstanceDynamic* GetOrCreateTransientDMI(UMaterialInstanceDynamic* InDMI, FName InDMIName, UMaterialInterface* InMaterialInterface, EObjectFlags InAdditionalObjectFlags);
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	static FVector RGBtoYUV(const FLinearColor& InColor);
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	static void DebugFunction();
	
};
