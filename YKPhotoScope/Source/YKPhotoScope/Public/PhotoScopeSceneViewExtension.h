#pragma once

#include "CoreMinimal.h"
#include "RHIGPUReadback.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"

#include "PhotoScopeSceneViewExtension.generated.h"

class UPhotoScopeSubsystem;

UENUM(BlueprintType)
enum class EScopeMode : uint8
{
	Parade		= 0		UMETA(ToolTip="分量图"),
	Waveform	= 1		UMETA(ToolTip="波形图"),
	VectorScope = 2		UMETA(ToolTip="矢量图"),
	Histogram	= 3		UMETA(ToolTip="直方图"),
};

UENUM(BlueprintType)
enum class EScopeDrawLocation : uint8
{
	AfterTonemapping = 0	UMETA(DisplayName="After Tonemapping"),
	BeforePost = 1			UMETA(DisplayName="Before PostProcess"),
	AfterMotionBlur = 2		UMETA(DisplayName="After Motion Blur"),
};

USTRUCT(BlueprintType)
struct FScopeConfig
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scope Settings")
	EScopeMode ScopeMode = EScopeMode::Waveform;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scope Settings")
	EScopeDrawLocation DrawLocation = EScopeDrawLocation::AfterTonemapping;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scope Settings")
	float MaxValue = 1.0f;
};

// Draw request with configuration
USTRUCT(BlueprintType)
struct FScopesDrawRequest
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TWeakObjectPtr<UTextureRenderTarget2D> TargetRT;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bPersistenceRequest = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scope Config")
	FScopeConfig ScopeConfig;
	
	// Equality operator for Array operations
	bool operator==(const FScopesDrawRequest& Other) const
	{
		return TargetRT == Other.TargetRT;
	}
	
};

// Scope data structure for CPU-side processing
struct FScopeData
{
	// Waveform data
	TArray<FVector> WaveformData;
	
	// Histogram data
	TArray<float> HistogramData;
	
	// Vector scope data
	TArray<FVector2D> VectorScopeData;
	
	// Parade data
	TArray<FVector> ParadeData;
	
	// Statistics
	float AverageLuminance;
	float MaxLuminance;
	float MinLuminance;
	FVector AverageColor;
	
	// Resolution info
	int32 Width;
	int32 Height;
};

USTRUCT(BlueprintType)
struct FScopesColorPickConfig
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnable = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Radius = 20;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D MouseUV = FVector2D(0.5f, 0.5f);
};

BEGIN_SHADER_PARAMETER_STRUCT(FScopeColorPickerParameters, )
	SHADER_PARAMETER(FVector2f, MouseUV)
	SHADER_PARAMETER(int32, Radius)
END_SHADER_PARAMETER_STRUCT()


class FPhotoScopeSceneViewExtension : public FSceneViewExtensionBase
{
	
public:
	FPhotoScopeSceneViewExtension(const FAutoRegister& AutoRegister);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {};
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};

	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs) override;
	
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;
	
	FScreenPassTexture DrawScopesPostMotionBlur(FRDGBuilder& GraphBuilder, const FSceneView& SceneView, const FPostProcessMaterialInputs& Inputs);
	FScreenPassTexture DrawScopesPostTonemap(FRDGBuilder& GraphBuilder, const FSceneView& SceneView, const FPostProcessMaterialInputs& Inputs);
	
	void DrawScopesAtLocation(FRDGBuilder& GraphBuilder, const FSceneView& SceneView, const FScreenPassTexture& SceneColor, EScopeDrawLocation Location);

	// Draw request management
	void AddScopesDrawRequest(const FScopesDrawRequest& DrawRequest);
	void RemoveScopesDrawRequest(UTextureRenderTarget2D* TargetRT);
	void ClearAllScopesDrawRequests();
	bool IsScopesDrawRequestActive(UTextureRenderTarget2D* TargetRT) const;
	
	FLinearColor GetPickedColor(UTextureRenderTarget2D* TargetRT) const;
	
	TArray<FScopesDrawRequest> AllDrawRequests;
	
	// Scope data for CPU-side access
	FScopeData ScopeData;
	
	FScopesColorPickConfig ColorPickConfig;

private:
	UPhotoScopeSubsystem* PhotoScopeSubsystem;

	// PooledRenderTarget cache to avoid recreating every frame
	TMap<UTextureRenderTarget2D*, TRefCountPtr<IPooledRenderTarget>> PooledRenderTargetCache;

	// 缓存RT尺寸避免频繁调整
	TMap<UTextureRenderTarget2D*, FIntPoint> RTDimensionsCache;
	
	static const int32 MAX_READBACK_BUFFERS = 4;

	struct FScopeReadbackContext
	{
		TArray<FRHIGPUBufferReadback*> Buffers;
		int32 WriteIndex = 0;
		int32 PendingNum = 0;

		FScopeReadbackContext()
		{
			for (int32 i = 0; i < MAX_READBACK_BUFFERS; ++i)
				Buffers.Add(new FRHIGPUBufferReadback(TEXT("PhotoScopeReadback")));
		}
        
		~FScopeReadbackContext()
		{
			for (auto* B : Buffers) delete B;
		}
	};

	TMap<TWeakObjectPtr<UTextureRenderTarget2D>, TUniquePtr<FScopeReadbackContext>> ReadbackContexts;
	TMap<TWeakObjectPtr<UTextureRenderTarget2D>, FLinearColor> ScopeResultsMap;
	
	void CreatePooledRenderTarget_RenderThread(UTextureRenderTarget2D* InRT, TRefCountPtr<IPooledRenderTarget>& OutPooledRenderTarget);

	bool IsActiveViewport(const FSceneView& InView);
	
	bool UpdateRTDimensionsIfNeeded(UTextureRenderTarget2D* TargetRT, const FIntPoint& SceneColorSize, EScopeMode ScopeMode);
	
	TRefCountPtr<IPooledRenderTarget> GetOrCreatePooledRenderTarget(UTextureRenderTarget2D* TargetRT);
	
	void ExecuteScopeShader(FRDGBuilder& GraphBuilder, const FGlobalShaderMap* GlobalShaderMap,
		const FScreenPassTexture& SceneColourTexture, const FScreenPassTextureViewport& SceneColorViewport,
		const FScopesDrawRequest& Request, TRefCountPtr<IPooledRenderTarget> LocalPooledRenderTarget);
	
	bool ProcessDrawRequest(FRDGBuilder& GraphBuilder, const FGlobalShaderMap* GlobalShaderMap,
		const FScreenPassTexture& SceneColourTexture, const FScreenPassTextureViewport& SceneColorViewport,
		const FScopesDrawRequest& Request, const FIntPoint& SceneColorSize);
	
	void GetColorPickerParameters(FScopeColorPickerParameters& Parameters) const;
	void ProcessReadbacks_RenderThread();
};

class YKPHOTOSCOPE_API FScopesDrawShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FScopesDrawShader)
	SHADER_USE_PARAMETER_STRUCT(FScopesDrawShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, SceneColorViewport)
		SHADER_PARAMETER_STRUCT(FScopeColorPickerParameters, ColorPicker)
		SHADER_PARAMETER(FVector2f, TextureSize)
		SHADER_PARAMETER(int32, ScopeMode)
		SHADER_PARAMETER(float, MaxValue)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OriginalSceneColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputR)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputG)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputB)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutColorAccumBuffer)
	END_SHADER_PARAMETER_STRUCT()
	
	// Basic shader initialization
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	// Define environment variables used by compute shader
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADS_X"), 8);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), 8);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), 1);
	}
};

class FScopesCombineCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FScopesCombineCS)
	SHADER_USE_PARAMETER_STRUCT(FScopesCombineCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, AccumR)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, AccumG)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, AccumB)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutFinalTarget)
		SHADER_PARAMETER(FVector2f, TextureSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FPhotoScopeColorPickerCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPhotoScopeColorPickerCS);
	SHADER_USE_PARAMETER_STRUCT(FPhotoScopeColorPickerCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, SceneColorViewport)
		SHADER_PARAMETER_STRUCT(FScopeColorPickerParameters, ColorPicker)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OriginalSceneColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
	END_SHADER_PARAMETER_STRUCT()
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADS_X"), 8);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), 8);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), 1);
	}
};
