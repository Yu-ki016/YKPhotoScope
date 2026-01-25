#include "PhotoScopeInputProcessor.h"

#include "PhotoScopeSubsystem.h"


void FPhotoScopeInputProcessor::Tick( const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor )
{
}

bool FPhotoScopeInputProcessor::HandleKeyDownEvent( FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent )
{
	// UE_LOG(LogTemp, Log, TEXT("FPhotoScopeInputProcessor::HandleKeyDownEvent, OnKeyDown: %s"), *InKeyEvent.GetKey().ToString());
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (UPhotoScopeSubsystem* Subsystem = OwningSubsystem.Get())
		{
			Subsystem->BroadcastEscKeyPressed();
		}
	}

	return IInputProcessor::HandleKeyDownEvent(SlateApp, InKeyEvent);
}
