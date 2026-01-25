#pragma once

#include "CoreMinimal.h"
#include "PhotoScopeSceneViewExtension.h"
#include "Subsystems/EngineSubsystem.h"
#include "PhotoScopeSubsystem.generated.h"

struct FScopesDrawRequest;
class FPhotoScopeInputProcessor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEscKeyPressed);

UCLASS(Config = EditorPerProjectUserSettings)
class YKPHOTOSCOPE_API UPhotoScopeSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
	
public:
	uint32 LastViewportId = -1;
	FIntPoint LastViewportSize = FIntPoint::ZeroValue;
	FIntPoint LastMousePos = FIntPoint::ZeroValue;
	
	UPROPERTY(BlueprintAssignable, Category = "Photo Scope")
	FOnEscKeyPressed OnEscKeyPressed;
	
	UPROPERTY(Config, EditAnywhere, Category = "Photo Scope")
	TMap<FIntPoint, FScopeConfig> SavedScopeConfigs;
	
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	
	void BroadcastEscKeyPressed() const;
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	void RegisterDrawRequest(const FScopesDrawRequest& DrawRequest);
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	void UpdateScopeDraw(const FScopesDrawRequest& DrawRequest);
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope", meta = (DisplayName = "Remove Scope Draw"))
	void UnregisterDrawRequest(UTextureRenderTarget2D* TargetRT);
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	void ClearAllDrawRequests();
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	bool IsDrawRequestActive(UTextureRenderTarget2D* TargetRT) const;
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	int32 InitialActiveViewport();
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	int32 UpdateActiveViewportAndMousePos(FIntPoint& OutViewportSize, FIntPoint& OutMousePos);
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	void SetColorPicker(FScopesColorPickConfig InConfig);
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	FLinearColor GetPickedColor(UTextureRenderTarget2D* TargetRT) const;
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	FVector2D GetLastMouseUV() const;
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	bool GetScopeConfig(FIntPoint Index, FScopeConfig& OutConfig) const;
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	void SaveScopeConfig(FIntPoint Index, const FScopeConfig& InConfig);
	
	UFUNCTION(BlueprintCallable, Category = "Photo Scope")
	void ClearScopeConfigs();
	
private:
	TSharedPtr<class FPhotoScopeSceneViewExtension, ESPMode::ThreadSafe> PhotoScopeSceneViewExtension;
	
	TSharedPtr<FPhotoScopeInputProcessor> PhotoScopeInputProcessor;
};
