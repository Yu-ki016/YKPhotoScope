#pragma once
#include "Framework/Application/IInputProcessor.h"

class UPhotoScopeSubsystem;

class FPhotoScopeInputProcessor : public IInputProcessor
{
public:
	explicit FPhotoScopeInputProcessor( const TWeakObjectPtr<UPhotoScopeSubsystem>& OwningSubsystem )
		: OwningSubsystem(OwningSubsystem)
	{
	}

	virtual void Tick( const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor ) override;

	virtual bool HandleKeyDownEvent( FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent ) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("FPhotoScopeInputProcessor"); }
	
private:
	TWeakObjectPtr<UPhotoScopeSubsystem> OwningSubsystem;
};
