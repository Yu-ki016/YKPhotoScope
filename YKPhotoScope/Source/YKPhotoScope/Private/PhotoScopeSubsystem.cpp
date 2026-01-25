#include "PhotoScopeSubsystem.h"

#include "PhotoScopeInputProcessor.h"
#include "PhotoScopeSceneViewExtension.h"
#include "SceneViewExtension.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/SceneViewport.h"

void UPhotoScopeSubsystem::Initialize( FSubsystemCollectionBase& Collection )
{
	PhotoScopeSceneViewExtension = FSceneViewExtensions::NewExtension<FPhotoScopeSceneViewExtension>();
	UE_LOG(LogTemp, Log, TEXT("UPhotoScopeSubsystem: Subsystem initialized & SceneViewExtension created"));

	// Set up active functor to always be active
	FSceneViewExtensionIsActiveFunctor IsActiveFunctor;
	IsActiveFunctor.IsActiveFunction = [](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
	{
		return TOptional<bool>(true);
	};
	PhotoScopeSceneViewExtension->IsActiveThisFrameFunctions.Add(IsActiveFunctor);
	
	PhotoScopeInputProcessor = MakeShared<FPhotoScopeInputProcessor>(this);
	FSlateApplication::Get().RegisterInputPreProcessor(PhotoScopeInputProcessor);
}

void UPhotoScopeSubsystem::Deinitialize()
{
	// Clear all draw requests
	if (PhotoScopeSceneViewExtension.IsValid())
	{
		PhotoScopeSceneViewExtension->AllDrawRequests.Empty();
		
		// Set inactive functor
		PhotoScopeSceneViewExtension->IsActiveThisFrameFunctions.Empty();

		FSceneViewExtensionIsActiveFunctor IsActiveFunctor;
		IsActiveFunctor.IsActiveFunction = [](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
		{
			return TOptional<bool>(false);
		};

		PhotoScopeSceneViewExtension->IsActiveThisFrameFunctions.Add(IsActiveFunctor);
	}

	PhotoScopeSceneViewExtension.Reset();
	PhotoScopeSceneViewExtension = nullptr;

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(PhotoScopeInputProcessor);
		PhotoScopeInputProcessor.Reset();
	}
}

void UPhotoScopeSubsystem::BroadcastEscKeyPressed() const
{
	if (OnEscKeyPressed.IsBound())
	{
		OnEscKeyPressed.Broadcast();
	}
}

void UPhotoScopeSubsystem::RegisterDrawRequest(const FScopesDrawRequest& DrawRequest)
{
	if (PhotoScopeSceneViewExtension.IsValid())
	{
		PhotoScopeSceneViewExtension->AddScopesDrawRequest(DrawRequest);
	}
}

void UPhotoScopeSubsystem::UnregisterDrawRequest(UTextureRenderTarget2D* TargetRT)
{
	if (PhotoScopeSceneViewExtension.IsValid())
	{
		PhotoScopeSceneViewExtension->RemoveScopesDrawRequest(TargetRT);
	}
}

void UPhotoScopeSubsystem::ClearAllDrawRequests()
{
	if (PhotoScopeSceneViewExtension.IsValid())
	{
		PhotoScopeSceneViewExtension->ClearAllScopesDrawRequests();
		UE_LOG(LogTemp, Verbose, TEXT("UPhotoScopeSubsystem: Cleared all draw requests"));
	}
}

bool UPhotoScopeSubsystem::IsDrawRequestActive(UTextureRenderTarget2D* TargetRT) const
{
	if (PhotoScopeSceneViewExtension.IsValid())
	{
		return PhotoScopeSceneViewExtension->IsScopesDrawRequestActive(TargetRT);
	}
	return false;
}

void UPhotoScopeSubsystem::UpdateScopeDraw(const FScopesDrawRequest& DrawRequest)
{
	UTextureRenderTarget2D* TargetRT = DrawRequest.TargetRT.Get();
	if (!TargetRT || !PhotoScopeSceneViewExtension.IsValid()) return;
	
	for (auto& Request : PhotoScopeSceneViewExtension->AllDrawRequests)
	{
		if (Request.TargetRT.Get() == TargetRT)
		{
			Request.ScopeConfig = DrawRequest.ScopeConfig;
			break;
		}
	}
}

int32 UPhotoScopeSubsystem::InitialActiveViewport()
{
	if (!GEditor) return -1;
	
	FViewport* ActiveViewport = GEditor->GetActiveViewport();
	for (FEditorViewportClient* EditorViewport : GEditor->GetAllViewportClients())
	{
		if (ActiveViewport == EditorViewport->Viewport && EditorViewport->ViewState.GetReference() != nullptr)
		{
			LastViewportId = EditorViewport->ViewState.GetReference()->GetViewKey();
			LastViewportSize = ActiveViewport->GetSizeXY();
			LastMousePos = FIntPoint(EditorViewport->GetCachedMouseX(), EditorViewport->GetCachedMouseY());
			break;
		}
	}
	
	return LastViewportId;
}

int32 UPhotoScopeSubsystem::UpdateActiveViewportAndMousePos( FIntPoint& OutViewportSize, FIntPoint& OutMousePos )
{
	OutMousePos = FIntPoint::ZeroValue;
	if (!GEditor) return -1;
	
	for (auto ViewportClient : GEditor->GetAllViewportClients())
	{
		FSceneViewport* SceneViewport = static_cast<FSceneViewport*>(ViewportClient->Viewport);
		if (!SceneViewport) continue;
		TSharedPtr<SWidget> ViewportWidget = SceneViewport->GetWidget().Pin();
		if (!ViewportWidget.IsValid()) continue;
		
		if (ViewportWidget->IsHovered())
		{
			LastViewportId = ViewportClient->ViewState.GetReference()->GetViewKey();
			LastViewportSize = ViewportClient->Viewport->GetSizeXY();
			LastMousePos = FIntPoint(ViewportClient->GetCachedMouseX(), ViewportClient->GetCachedMouseY());
			break;
		}
	}
	
	OutViewportSize = LastViewportSize;
	OutMousePos = LastMousePos;
	return LastViewportId;
}

void UPhotoScopeSubsystem::SetColorPicker(FScopesColorPickConfig InConfig)
{
	if (PhotoScopeSceneViewExtension.IsValid())
	{
		PhotoScopeSceneViewExtension->ColorPickConfig = InConfig;
	}
}

FLinearColor UPhotoScopeSubsystem::GetPickedColor( UTextureRenderTarget2D* TargetRT ) const
{
	return PhotoScopeSceneViewExtension ? PhotoScopeSceneViewExtension->GetPickedColor(TargetRT) : FLinearColor::Black;
}

FVector2D UPhotoScopeSubsystem::GetLastMouseUV() const
{
	if (LastViewportSize == FIntPoint::ZeroValue) return FVector2D::ZeroVector;
	return FVector2D(LastMousePos) / FVector2D(LastViewportSize);
}

bool UPhotoScopeSubsystem::GetScopeConfig( FIntPoint Index, FScopeConfig& OutConfig ) const
{
	if(SavedScopeConfigs.Find(Index))
	{
		OutConfig = SavedScopeConfigs[Index];
		return true;
	};
	
	return false;
}

void UPhotoScopeSubsystem::SaveScopeConfig( FIntPoint Index, const FScopeConfig& InConfig )
{
	SavedScopeConfigs.FindOrAdd(Index) = InConfig;
	this->SaveConfig();
}

void UPhotoScopeSubsystem::ClearScopeConfigs()
{
	SavedScopeConfigs.Empty();
}
