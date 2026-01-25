#include "PhotoScopeBlueprintLibrary.h"

#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Components/GridPanel.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Slate/SceneViewport.h"

void UPhotoScopeBlueprintLibrary::SetGridPanelLayout(UObject* WorldContextObject, UGridPanel* GridPanel, TSubclassOf<UUserWidget> ContentClass, FIntPoint Layout )
{
	if (!GEditor) return;

	GridPanel->ColumnFill.Empty();
	GridPanel->RowFill.Empty();
	
	for (int x = 0; x < Layout.X; x++)
	{
		GridPanel->SetColumnFill(x, 1.0f);
	}

	for (int y = 0; y < Layout.Y; ++y) 
	{
		GridPanel->SetRowFill(y, 1.0f);
	}
	
	GridPanel->SynchronizeProperties();
	UWorld* World = GEngine->GetWorldFromContextObjectChecked(WorldContextObject);
	for (int x = 0; x < Layout.X; x++)
	{
		for (int y = 0; y < Layout.Y; ++y)
		{
			UUserWidget* Content = CreateWidget<UUserWidget>(World, ContentClass);
			GridPanel->AddChildToGrid(Content, y, x);
		}
	}

	
}

UTextureRenderTarget2D* UPhotoScopeBlueprintLibrary::UpdateOrCreateRenderTarget(
    const UObject* WorldContextObject,
    UTextureRenderTarget2D* TargetRT,
    int32 Width,
    int32 Height,
    ETextureRenderTargetFormat Format,
    bool bSRGB,
    bool bSupportUAVs,
    FLinearColor ClearColor,
    bool bAllowDynamicResizing
)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World) return nullptr;
    
	bool bNeedsRecreate = true;
    
	// 如果允许动态调整，且RT已存在，则不立即重建，由示波器系统动态处理
	if (TargetRT)
	{
		// 检查RT是否有效且格式匹配
		if (TargetRT->RenderTargetFormat != Format)
		{
			bNeedsRecreate = true;
		}
		// 如果不允许动态调整，则检查尺寸
		else if (!bAllowDynamicResizing)
		{
			if (TargetRT->SizeX != Width || TargetRT->SizeY != Height)
			{
				bNeedsRecreate = true;
			}
		}
	}
	else
	{
		bNeedsRecreate = true;
	}
    
	if (bNeedsRecreate)
	{
		// 创建新RT
		TargetRT = UKismetRenderingLibrary::CreateRenderTarget2D(World, Width, Height, Format, ClearColor, false, bSupportUAVs);
        
		if (TargetRT)
		{
			TargetRT->SRGB = bSRGB;
			TargetRT->ClearColor = ClearColor;
			TargetRT->UpdateResource();
            
			UE_LOG(LogTemp, Log, TEXT("Created new RT: %s (%dx%d, Format: %d)"), 
				*TargetRT->GetName(), Width, Height, static_cast<int32>(Format));
		}
	}
	else
	{
		TargetRT->SRGB = bSRGB;
		TargetRT->ClearColor = ClearColor;
        
		// 如果尺寸不匹配但允许动态调整，不重建RT，由示波器系统处理
		if (bAllowDynamicResizing && (TargetRT->SizeX != Width || TargetRT->SizeY != Height))
		{
			UE_LOG(LogTemp, Log, TEXT("RT %s will be dynamically resized by scope system (Current: %dx%d, Requested: %dx%d)"), 
				*TargetRT->GetName(), TargetRT->SizeX, TargetRT->SizeY, Width, Height);
		}
	}
	
	return TargetRT;
}

void UPhotoScopeBlueprintLibrary::ForceRedrawEditor()
{
	if (GEditor)
	{
		GEditor->RedrawAllViewports(false); // false 表示不强制立即重绘，而是标记为待重绘
	}
}

FIntPoint UPhotoScopeBlueprintLibrary::GetActiveViewportBufferSize()
{
	FIntPoint ViewportSize = FIntPoint::ZeroValue;
	if (GEditor && GEditor->GetActiveViewport())
	{
		ViewportSize = GEditor->GetActiveViewport()->GetRenderTargetTextureSizeXY();
	}
	return ViewportSize;
}

EObjectFlags GetTransientDMIFlags()
{
	return RF_Transient | RF_NonPIEDuplicateTransient | RF_TextExportTransient;
}


UMaterialInstanceDynamic* UPhotoScopeBlueprintLibrary::GetOrCreateTransientDMI( UMaterialInstanceDynamic* InDMI,
	FName InDMIName, UMaterialInterface* InMaterialInterface, EObjectFlags InAdditionalObjectFlags )
{
	if (!IsValid(InMaterialInterface))
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* ResultDMI = InDMI;

	// If there's no MID yet or if the MID's parent material interface (could be a Material or a MIC) doesn't match the requested material interface (could be a Material, MIC or MID), create a new one : 
	if (!IsValid(InDMI) || (InDMI->Parent != InMaterialInterface))
	{
		// If the requested material is already a UMaterialInstanceDynamic, we can use it as is :
		ResultDMI = Cast<UMaterialInstanceDynamic>(InMaterialInterface);

		if (ResultDMI != nullptr)
		{
			ensure(EnumHasAnyFlags(InMaterialInterface->GetFlags(), EObjectFlags::RF_Transient)); // The name of the function implies we're dealing with transient MIDs
		}
		else
		{
			// If it's not a UMaterialInstanceDynamic, it's a UMaterialInstanceConstant or a UMaterial, both of which can be used to create a MID : 
			ResultDMI = UMaterialInstanceDynamic::Create(InMaterialInterface, nullptr, MakeUniqueObjectName(GetTransientPackage(), UMaterialInstanceDynamic::StaticClass(), InDMIName));
			ResultDMI->SetFlags(InAdditionalObjectFlags);
		}
	}

	check(ResultDMI != nullptr);
	return ResultDMI;
}

UMaterialInstanceDynamic* UPhotoScopeBlueprintLibrary::GetOrCreateTransientDMI( UMaterialInstanceDynamic* InDMI,
	FName InDMIName, UMaterialInterface* InMaterialInterface )
{
	return GetOrCreateTransientDMI(InDMI, InDMIName, InMaterialInterface, GetTransientDMIFlags());
}

FVector UPhotoScopeBlueprintLibrary::RGBtoYUV( const FLinearColor& InColor )
{
	float R = InColor.R;
	float G = InColor.G;
	float B = InColor.B;
	float Y = 0.2126f * R + 0.7152f * G + 0.0722f * B;
	float U = -0.114572f * R - 0.385428f * G + 0.5f * B;
	float V = 0.5f * R - 0.454153f * G - 0.045847f * B;
	return FVector(Y, U, V);
}

void UPhotoScopeBlueprintLibrary::DebugFunction()
{
	
}
