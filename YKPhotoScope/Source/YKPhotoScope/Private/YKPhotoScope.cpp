// Copyright Epic Games, Inc. All Rights Reserved.

#include "YKPhotoScope.h"

#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FYKPhotoScopeModule"

void FYKPhotoScopeModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("YKPhotoScope"))->GetBaseDir(), TEXT("Shaders"));
	if(!AllShaderSourceDirectoryMappings().Contains(TEXT("/Plugins/YKPhotoScope")))
	{
		AddShaderSourceDirectoryMapping(TEXT("/Plugins/YKPhotoScope"), PluginShaderDir);
	}
}

void FYKPhotoScopeModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FYKPhotoScopeModule, YKPhotoScope)