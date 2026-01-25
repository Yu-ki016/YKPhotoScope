#include "PhotoScopeSceneViewExtension.h"

#include "PhotoScopeSubsystem.h"
#include "RenderTargetPool.h"
#include "Engine/TextureRenderTarget2D.h"
#include "PostProcess/PostProcessInputs.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "Runtime/Renderer/Private/SceneRendering.h"

IMPLEMENT_GLOBAL_SHADER(FScopesDrawShader, "/Plugins/YKPhotoScope/PhotoScopesCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FScopesCombineCS, "/Plugins/YKPhotoScope/PhotoScopesCS.usf", "CombineRTCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPhotoScopeColorPickerCS, "/Plugins/YKPhotoScope/ColorPickerCS.usf", "MainCS", SF_Compute);

FPhotoScopeSceneViewExtension::FPhotoScopeSceneViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister)
{
	if (GEngine)
	{
		PhotoScopeSubsystem = GEngine->GetEngineSubsystem<UPhotoScopeSubsystem>();
	}
	
	ScopeData.AverageLuminance = 0.0f;
	ScopeData.MaxLuminance = 0.0f;
	ScopeData.MinLuminance = 1.0f;
	ScopeData.AverageColor = FVector(0.0f, 0.0f, 0.0f);
	ScopeData.Width = 0;
	ScopeData.Height = 0;
}

// Subscribe to post-processing passes based on draw location
void FPhotoScopeSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (PassId == EPostProcessingPass::MotionBlur)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FPhotoScopeSceneViewExtension::DrawScopesPostMotionBlur));
	}
	if (PassId == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FPhotoScopeSceneViewExtension::DrawScopesPostTonemap));
	}
}


void FPhotoScopeSceneViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs)
{
	if (!IsActiveViewport(InView)) return;
	checkSlow(InView.bIsViewInfo);
	const FIntRect Viewport = static_cast<const FViewInfo&>(InView).ViewRect;
	const FScreenPassTexture SceneColourTexture((*Inputs.SceneTextures)->SceneColorTexture, Viewport);
	
	ProcessReadbacks_RenderThread();
	DrawScopesAtLocation(GraphBuilder, InView, SceneColourTexture, EScopeDrawLocation::BeforePost);
}

FScreenPassTexture FPhotoScopeSceneViewExtension::DrawScopesPostMotionBlur( FRDGBuilder& GraphBuilder,
	const FSceneView& SceneView, const FPostProcessMaterialInputs& Inputs )
{
	const FScreenPassTexture& SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	
	DrawScopesAtLocation(GraphBuilder, SceneView, SceneColor, EScopeDrawLocation::AfterMotionBlur);
	
	FScreenPassRenderTarget OverrideOutput = Inputs.OverrideOutput;
	
	FScreenPassTextureSlice& SceneColorSlice = const_cast<FScreenPassTextureSlice&>(Inputs.Textures[(uint32)EPostProcessMaterialInput::SceneColor]);
	return FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColorSlice, static_cast<FScreenPassTexture>(OverrideOutput));
	
}

FScreenPassTexture FPhotoScopeSceneViewExtension::DrawScopesPostTonemap(FRDGBuilder& GraphBuilder, const FSceneView& SceneView, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture& SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	
	DrawScopesAtLocation(GraphBuilder, SceneView, SceneColor, EScopeDrawLocation::AfterTonemapping);

	FScreenPassRenderTarget OverrideOutput = Inputs.OverrideOutput;
	
	if (!ColorPickConfig.bEnable || !IsActiveViewport(SceneView))
	{
		FScreenPassTextureSlice& SceneColorSlice = const_cast<FScreenPassTextureSlice&>(Inputs.Textures[(uint32)EPostProcessMaterialInput::SceneColor]);
		return FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColorSlice, static_cast<FScreenPassTexture>(OverrideOutput));
	}
	
	if (!OverrideOutput.IsValid())
	{
		OverrideOutput = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, SceneView.GetOverwriteLoadAction(), TEXT("PhotoScopeRenderTarget"));
	}
	
	const FSceneViewFamily& ViewFamily = *SceneView.Family;
	const FScreenPassTextureViewport SceneColorViewport(SceneColor);
	
	RDG_EVENT_SCOPE(GraphBuilder, "YK Photo Scope ColorPicker");
	{
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ViewFamily.GetFeatureLevel());
	
		FRDGTextureDesc OutputDesc;
		{
			OutputDesc = OverrideOutput.Texture->Desc;
	
			OutputDesc.Flags |= TexCreate_UAV;
			OutputDesc.Flags &= ~(TexCreate_RenderTargetable | TexCreate_FastVRAM);
	
			FLinearColor ClearColor(0., 0., 0., 0.);
			OutputDesc.ClearValue = FClearValueBinding(ClearColor);
		}
	
		FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("PhotoScopeColorPickerTexture"));
	
		FPhotoScopeColorPickerCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPhotoScopeColorPickerCS::FParameters>();
	
		PassParameters->OriginalSceneColor = SceneColor.Texture;
		PassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);
	
		FIntPoint PassViewSize = SceneColor.ViewRect.Size();
		PassParameters->Output = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));
		GetColorPickerParameters(PassParameters->ColorPicker);
	
		FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize, FComputeShaderUtils::kGolden2DGroupSize);
	
		TShaderMapRef<FPhotoScopeColorPickerCS> ComputeShader(GlobalShaderMap);
	
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PhotoScopeColorPicker CS Shader %dx%d", PassViewSize.X, PassViewSize.Y),
			ComputeShader,
			PassParameters,
			GroupCount);

		AddCopyTexturePass(GraphBuilder, OutputTexture, OverrideOutput.Texture);
	}
	
	return static_cast<FScreenPassTexture>(OverrideOutput);
}


bool FPhotoScopeSceneViewExtension::IsActiveViewport(const FSceneView& InView)
{
	if (PhotoScopeSubsystem && PhotoScopeSubsystem->LastViewportId != -1)
	{
		return InView.GetViewKey() == PhotoScopeSubsystem->LastViewportId;
	}
	
	if (InView.Family->AllViews[0] != &InView) return false;
	
	UWorld* InWorld =  InView.Family->Scene ? InView.Family->Scene->GetWorld() : nullptr;
	if (!InWorld) return false;
	
	EWorldType::Type WorldType = InWorld->WorldType;

	if (WorldType == EWorldType::Type::Editor || 
		WorldType == EWorldType::Type::Game ||
		WorldType == EWorldType::Type::PIE) 
		return true;

	return false;
}

bool FPhotoScopeSceneViewExtension::UpdateRTDimensionsIfNeeded(UTextureRenderTarget2D* TargetRT, const FIntPoint& SceneColorSize, EScopeMode ScopeMode)
{
	// Calculate desired RT size based on scope mode
	FIntPoint DesiredRTSize;
	
	// Different modes have different RT dimensions
	switch (ScopeMode)
	{
	case EScopeMode::Parade:
	case EScopeMode::Waveform:
		DesiredRTSize = FIntPoint(SceneColorSize.X, 256);
		break;
	case EScopeMode::VectorScope:
		DesiredRTSize = FIntPoint(256, 256);
		break;
	case EScopeMode::Histogram:
		DesiredRTSize = FIntPoint(256, 1);
		break;
	default:
		DesiredRTSize = FIntPoint(256, 256);
		break;
	}
	
	// Check if RT dimensions need to be adjusted
	bool bNeedResize = false;
	if (!RTDimensionsCache.Contains(TargetRT) || RTDimensionsCache[TargetRT] != DesiredRTSize)
	{
		bNeedResize = true;
	}

	// Resize RT if needed
	if (bNeedResize)
	{
		if (RTDimensionsCache.Contains(TargetRT))
		{
			RTDimensionsCache[TargetRT] = DesiredRTSize;
		}
		else
		{
			RTDimensionsCache.Add(TargetRT, DesiredRTSize);
		}

		// Clear the PooledRenderTarget cache for this RT since dimensions changed
		// We'll create a new one in the next frame when RT resize is complete
		PooledRenderTargetCache.Remove(TargetRT);

		// Resize RT on the game thread, not render thread
		// because InitCustomFormat() cannot be called from render thread
		AsyncTask(ENamedThreads::GameThread, [TargetRT, DesiredRTSize]()
		{
			if (TargetRT)
			{
				// Resize the render target on game thread
				TargetRT->InitCustomFormat(DesiredRTSize.X, DesiredRTSize.Y, PF_FloatRGBA, false);
				TargetRT->SRGB = false;
				TargetRT->UpdateResource();
				UE_LOG(LogTemp, Log, TEXT("Resized RT %s to %dx%d"), *TargetRT->GetName(), DesiredRTSize.X, DesiredRTSize.Y);
			}
		});
	}

	return bNeedResize;
}

TRefCountPtr<IPooledRenderTarget> FPhotoScopeSceneViewExtension::GetOrCreatePooledRenderTarget(UTextureRenderTarget2D* TargetRT)
{
	const FTextureRenderTargetResource* RTResource = TargetRT ? TargetRT->GetRenderTargetResource() : nullptr;
	const FTextureRHIRef CurrentRHI = RTResource ? RTResource->GetRenderTargetTexture() : nullptr;

	if (!CurrentRHI.IsValid()) return nullptr;

	// Check if we already have a cached PooledRenderTarget for this RT
	if (PooledRenderTargetCache.Contains(TargetRT))
	{
		TRefCountPtr<IPooledRenderTarget>& CachedPooledRT = PooledRenderTargetCache[TargetRT];
        
		if (CachedPooledRT.IsValid() && CachedPooledRT->GetRHI() == CurrentRHI)
		{
			return CachedPooledRT;
		}
        
		// CachedPooledRT的RHI和TargetRT指针不匹配，说明物理资源变了，移除旧缓存(当切换Viewport时可能发生)
		PooledRenderTargetCache.Remove(TargetRT);
	}

	// No cache entry, create new and cache it
	TRefCountPtr<IPooledRenderTarget> NewPooledRT;
	CreatePooledRenderTarget_RenderThread(TargetRT, NewPooledRT);
    
	if (NewPooledRT.IsValid())
	{
		PooledRenderTargetCache.Add(TargetRT, NewPooledRT);
		return NewPooledRT;
	}

	return nullptr;
}

void FPhotoScopeSceneViewExtension::ExecuteScopeShader(FRDGBuilder& GraphBuilder, const FGlobalShaderMap* GlobalShaderMap, 
		const FScreenPassTexture& SceneColourTexture, const FScreenPassTextureViewport& SceneColorViewport, 
		const FScopesDrawRequest& Request, TRefCountPtr<IPooledRenderTarget> LocalPooledRenderTarget)
{
	RDG_EVENT_SCOPE(GraphBuilder, "Photo Scope");
	{
		FRDGTextureRef OutputRDG = GraphBuilder.RegisterExternalTexture(LocalPooledRenderTarget, TEXT("Scope Render Target"));
		FIntPoint DescSize = FIntPoint(OutputRDG->Desc.GetSize().X, OutputRDG->Desc.GetSize().Y);
		
		// 创建三个中间累加纹理 (原子操作不支持多通道格式)
		FRDGTextureDesc AccumDesc = FRDGTextureDesc::Create2D(
			DescSize, 
			PF_R32_UINT, 
			FClearValueBinding::None, 
			TexCreate_UAV | TexCreate_ShaderResource);

		FRDGTextureRef AccumR = GraphBuilder.CreateTexture(AccumDesc, TEXT("Waveform_Accum_R"));
		FRDGTextureRef AccumG = GraphBuilder.CreateTexture(AccumDesc, TEXT("Waveform_Accum_G"));
		FRDGTextureRef AccumB = GraphBuilder.CreateTexture(AccumDesc, TEXT("Waveform_Accum_B"));
		
		// 初始化纹理
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AccumR), static_cast<uint32>(0), ERDGPassFlags::Compute);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AccumG), static_cast<uint32>(0), ERDGPassFlags::Compute);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AccumB), static_cast<uint32>(0), ERDGPassFlags::Compute);
		
		FRDGBufferRef ColorAccumBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 4), TEXT("ScopeColorAccumBuffer"));
		
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ColorAccumBuffer), 0);
		
		FScopesDrawShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FScopesDrawShader::FParameters>();
	
		PassParameters->OriginalSceneColor = SceneColourTexture.Texture;
		PassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);
		PassParameters->TextureSize = FVector2f(OutputRDG->Desc.GetSize().X, OutputRDG->Desc.GetSize().Y);
		GetColorPickerParameters(PassParameters->ColorPicker);
		
		PassParameters->ScopeMode = static_cast<int32>(Request.ScopeConfig.ScopeMode);
		PassParameters->MaxValue = Request.ScopeConfig.MaxValue;
	
		// Create UAV from Target Texture
		PassParameters->OutputR = GraphBuilder.CreateUAV(AccumR);
		PassParameters->OutputG = GraphBuilder.CreateUAV(AccumG);
		PassParameters->OutputB = GraphBuilder.CreateUAV(AccumB);
		PassParameters->OutColorAccumBuffer = GraphBuilder.CreateUAV(ColorAccumBuffer);

		FIntPoint PassViewSize = SceneColorViewport.Rect.Size();
		FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize, FComputeShaderUtils::kGolden2DGroupSize);
	
		TShaderMapRef<FScopesDrawShader> ComputeShader(GlobalShaderMap);
	
		AddClearRenderTargetPass(GraphBuilder, OutputRDG, FLinearColor::Black);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Photo Scope Draw Pass %dx%d", PassViewSize.X, PassViewSize.Y),
			ComputeShader,
			PassParameters,
			GroupCount);
		
		{
			FScopesCombineCS::FParameters* ScopeCombinePassParameters = GraphBuilder.AllocParameters<FScopesCombineCS::FParameters>();
    
			ScopeCombinePassParameters->AccumR = AccumR;
			ScopeCombinePassParameters->AccumG = AccumG;
			ScopeCombinePassParameters->AccumB = AccumB;
			ScopeCombinePassParameters->OutFinalTarget = GraphBuilder.CreateUAV(OutputRDG);
			ScopeCombinePassParameters->TextureSize = FVector2f(OutputRDG->Desc.Extent.X, OutputRDG->Desc.Extent.Y);

			TShaderMapRef<FScopesCombineCS> ScopeCombineShader(GlobalShaderMap);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Scope Combine Pass"),
				ScopeCombineShader,
				ScopeCombinePassParameters,
				GroupCount);
		}
		
		if (ColorPickConfig.bEnable)
		{
			// 将Buffer里的数据拷贝到Readback Buffer以供CPU读取
			UTextureRenderTarget2D* CurrentRT = Request.TargetRT.Get();
			if (!CurrentRT) return;

			if (!ReadbackContexts.Contains(CurrentRT))
			{
				ReadbackContexts.Add(CurrentRT, MakeUnique<FScopeReadbackContext>());
			}
			FScopeReadbackContext& Context = *ReadbackContexts[CurrentRT];

			if (Context.PendingNum < MAX_READBACK_BUFFERS)
			{
				FRHIGPUBufferReadback* TargetBuffer = Context.Buffers[Context.WriteIndex];
				AddEnqueueCopyPass(GraphBuilder, TargetBuffer, ColorAccumBuffer, 0);
				Context.WriteIndex = (Context.WriteIndex + 1) % MAX_READBACK_BUFFERS;
				Context.PendingNum = FMath::Min(Context.PendingNum + 1, MAX_READBACK_BUFFERS);
			}
		}
	}
		
}

bool FPhotoScopeSceneViewExtension::ProcessDrawRequest(FRDGBuilder& GraphBuilder, const FGlobalShaderMap* GlobalShaderMap, 
		const FScreenPassTexture& SceneColourTexture, const FScreenPassTextureViewport& SceneColorViewport, 
		const FScopesDrawRequest& Request, const FIntPoint& SceneColorSize)
{
	if (!Request.TargetRT.Get()) return false;

	UTextureRenderTarget2D* TargetRT = Request.TargetRT.Get();
	FTextureRenderTargetResource* RTResource = TargetRT->GetRenderTargetResource();
	FTextureRHIRef TargetRHI = RTResource ? RTResource->GetRenderTargetTexture() : nullptr;
	if (!TargetRHI) return false;

	// Update RT dimensions if needed
	const bool bResized = UpdateRTDimensionsIfNeeded(TargetRT, SceneColorSize, Request.ScopeConfig.ScopeMode);
	if (bResized)
	{
		// Skip this frame's rendering for this RT since we're resizing it
		return false;
	}

	// Get or create pooled render target
	TRefCountPtr<IPooledRenderTarget> LocalPooledRenderTarget = GetOrCreatePooledRenderTarget(TargetRT);
	if (!LocalPooledRenderTarget.IsValid()) return false;

	ExecuteScopeShader(GraphBuilder, GlobalShaderMap, SceneColourTexture, SceneColorViewport, Request, LocalPooledRenderTarget);
	return true;
}

// Helper method to draw scopes at specific location
void FPhotoScopeSceneViewExtension::DrawScopesAtLocation(FRDGBuilder& GraphBuilder, const FSceneView& SceneView, const FScreenPassTexture& SceneColor, EScopeDrawLocation Location)
{
	if (!IsActiveViewport(SceneView)) return;
	
	// Clean up invalid draw requests with invalid RTs
	for (int32 i = AllDrawRequests.Num() - 1; i >= 0; i--)
	{
		const FScopesDrawRequest& Request = AllDrawRequests[i];
		if (!Request.TargetRT.IsValid())
		{
			AllDrawRequests.RemoveAt(i);
			UE_LOG(LogTemp, Log, TEXT("FPhotoScopeSceneViewExtension: Removed invalid draw request with null TargetRT"));
		}
	}
	
	if (AllDrawRequests.Num() < 1) return;
	
	checkSlow(SceneView.bIsViewInfo);
	
	const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	
	if (!SceneColor.IsValid()) return;
	const FScreenPassTextureViewport SceneColorViewport(SceneColor);
	FIntPoint SceneColorSize = SceneColor.ViewRect.Size();
	
	// Create a copy of draw requests to process
	TArray<FScopesDrawRequest> RequestsToProcess = AllDrawRequests;
	
	// Process each draw request that matches the current location
	for (int32 i = RequestsToProcess.Num() - 1; i >= 0; i--)
	{
		const auto& Request = RequestsToProcess[i];
		
		// Skip requests that don't match the current location
		if (Request.ScopeConfig.DrawLocation != Location)
		{
			continue;
		}
		
		// Process the draw request
		bool bRenderingCompleted = ProcessDrawRequest(GraphBuilder, GlobalShaderMap, SceneColor, SceneColorViewport, Request, SceneColorSize);
		
		// If this is not a persistence request AND rendering completed successfully, remove it
		if (!Request.bPersistenceRequest && bRenderingCompleted)
		{
			// Remove from the original array
			AllDrawRequests.Remove(Request);
			// Also remove from cache if needed
			PooledRenderTargetCache.Remove(Request.TargetRT.Get());
			RTDimensionsCache.Remove(Request.TargetRT.Get());
		}
	}
}

void FPhotoScopeSceneViewExtension::AddScopesDrawRequest(const FScopesDrawRequest& DrawRequest)
{
	if (!DrawRequest.TargetRT.IsValid()) return;

	bool bAlreadyRegistered = false;
	for (int32 i = 0; i < AllDrawRequests.Num(); i++)
	{
		if (AllDrawRequests[i].TargetRT == DrawRequest.TargetRT)
		{
			AllDrawRequests[i] = DrawRequest;
			bAlreadyRegistered = true;
			break;
		}
	}
	
	if (!bAlreadyRegistered)
	{
		AllDrawRequests.Add(DrawRequest);
		UE_LOG(LogTemp, Verbose, TEXT("FPhotoScopeSceneViewExtension: Added draw request for RT: %s"), 
			*DrawRequest.TargetRT.Get()->GetName());
	}
	
	UTextureRenderTarget2D* TargetRT = DrawRequest.TargetRT.Get();
	FIntPoint RTSize(TargetRT->SizeX, TargetRT->SizeY);
	if (RTDimensionsCache.Contains(TargetRT))
	{
		RTDimensionsCache[TargetRT] = RTSize;
	}
	else
	{
		RTDimensionsCache.Add(TargetRT, RTSize);
	}
}

void FPhotoScopeSceneViewExtension::RemoveScopesDrawRequest(UTextureRenderTarget2D* TargetRT)
{
	if (!TargetRT) return;

	int32 RemovedCount = AllDrawRequests.RemoveAll([TargetRT](const FScopesDrawRequest& Request)
	{
		return Request.TargetRT.Get() == TargetRT;
	});
	
	if (RemovedCount > 0)
	{
		PooledRenderTargetCache.Remove(TargetRT);
		RTDimensionsCache.Remove(TargetRT);
		ReadbackContexts.Remove(TargetRT);
		ScopeResultsMap.Remove(TargetRT);
		UE_LOG(LogTemp, Log, TEXT("FPhotoScopeSceneViewExtension: Removed %d draw request(s) for RT: %s and cleared cache"), RemovedCount, *TargetRT->GetName());
	}
}

void FPhotoScopeSceneViewExtension::ClearAllScopesDrawRequests()
{
	AllDrawRequests.Empty();
	PooledRenderTargetCache.Empty();
	RTDimensionsCache.Empty();
	ReadbackContexts.Empty();
	ScopeResultsMap.Empty();
	UE_LOG(LogTemp, Log, TEXT("FPhotoScopeSceneViewExtension: Cleared all draw requests and PooledRenderTarget cache"));
}

bool FPhotoScopeSceneViewExtension::IsScopesDrawRequestActive(UTextureRenderTarget2D* TargetRT) const
{
	if (!TargetRT) return false;

	for (const auto& Request : AllDrawRequests)
	{
		if (Request.TargetRT.Get() == TargetRT)
		{
			return true;
		}
	}
	return false;
}

void FPhotoScopeSceneViewExtension::CreatePooledRenderTarget_RenderThread(UTextureRenderTarget2D* InRT, TRefCountPtr<IPooledRenderTarget>& OutPooledRenderTarget)
{
	//是否在渲染线程或 RHI 线程中
	checkf(IsInRenderingThread() || IsInRHIThread(), TEXT("Cannot create from outside the rendering thread"));

	//渲染目标资源需要渲染线程
	// Render target resources require the render thread
	const FTextureRenderTargetResource* RenderTargetResource = InRT ? InRT->GetRenderTargetResource() : nullptr;

	if (RenderTargetResource == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Render Target Resource is null"));
		return;
	}

	//获取RHI 引用
	//获取渲染目标纹理的 RHI 引用
	//将纹理绑定到渲染管线上
	//可以使用 RHI 引用来创建、更新或销毁纹理
	const FTextureRHIRef RenderTargetRHI = RenderTargetResource->GetRenderTargetTexture();
	if (RenderTargetRHI.GetReference() == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Render Target RHI is null"));
		return;
	}

	//渲染目标秒速
	FSceneRenderTargetItem RenderTargetItem;
	//渲染目标  被着色器作为资源访问
	RenderTargetItem.TargetableTexture = RenderTargetRHI;
	RenderTargetItem.ShaderResourceTexture = RenderTargetRHI;

	const EPixelFormat PixelFormat = RenderTargetRHI->GetFormat();

	// 验证格式是否有效
	if (PixelFormat == PF_Unknown)
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid pixel format on RenderTexture!"));
		return;
	}
	
	FPooledRenderTargetDesc RenderTargetDesc = FPooledRenderTargetDesc::Create2DDesc(
		RenderTargetResource->GetSizeXY(), RenderTargetRHI->GetDesc().Format,
		FClearValueBinding::Black,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV, TexCreate_None, false);

	//创建一个新的池化渲染目标。这个方法的参数包括渲染目标的描述对象
	GRenderTargetPool.CreateUntrackedElement(RenderTargetDesc, OutPooledRenderTarget, RenderTargetItem);

	// Log only when we create a new resource
	UE_LOG(LogTemp, Verbose, TEXT("Created untracked Pooled Render Target resource for RT: %s"), *InRT->GetName());
}

void FPhotoScopeSceneViewExtension::GetColorPickerParameters(FScopeColorPickerParameters& Parameters) const
{
	Parameters.Radius = ColorPickConfig.bEnable ? ColorPickConfig.Radius : 0;
	Parameters.MouseUV = FVector2f(ColorPickConfig.MouseUV.X, ColorPickConfig.MouseUV.Y);
}

FLinearColor FPhotoScopeSceneViewExtension::GetPickedColor( UTextureRenderTarget2D* TargetRT ) const
{
	if (ScopeResultsMap.Contains(TargetRT))
	{
		return ScopeResultsMap[TargetRT];
	}
	return FLinearColor::Black;
}

void FPhotoScopeSceneViewExtension::ProcessReadbacks_RenderThread()
{
	if (!ColorPickConfig.bEnable) return;
	
	for (auto& It : ReadbackContexts)
	{
		TWeakObjectPtr<UTextureRenderTarget2D> WeakRT = It.Key;
		FScopeReadbackContext& Context = *It.Value;

		if (Context.PendingNum <= 0) continue;

		// 计算最旧的一个缓冲区的索引 (即最早写入、最可能准备好的数据)
		int32 ReadIndex = (Context.WriteIndex - Context.PendingNum + MAX_READBACK_BUFFERS) % MAX_READBACK_BUFFERS;
		FRHIGPUBufferReadback* ReadbackBuffer = Context.Buffers[ReadIndex];

		if (ReadbackBuffer->IsReady())
		{
			// Lock 并提取数据
			const uint32* RawData = (const uint32*)ReadbackBuffer->Lock(4 * sizeof(uint32));
			if (RawData)
			{
				if (RawData[3] > 0) // RawData[3]里保存的是像素累加计数
				{
					float InvCount = 1.0f / static_cast<float>(RawData[3]);
					FLinearColor ResultColor(
						(RawData[0] / 1000.0f) * InvCount,
						(RawData[1] / 1000.0f) * InvCount,
						(RawData[2] / 1000.0f) * InvCount,
						1.0f
					);

					AsyncTask(ENamedThreads::GameThread, [this, WeakRT, ResultColor]()
					{
						if (UTextureRenderTarget2D* RT = WeakRT.Get())
						{
							ScopeResultsMap.FindOrAdd(RT) = ResultColor;
						}
					});
				}
				ReadbackBuffer->Unlock();
			}
			Context.PendingNum--;
		}
	}
}