#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "TargetFrameWidget.generated.h"

class UProgressBar;
class UTextBlock;

/**
 * What the character is fighting, at the top middle of the world screen: its name over its health.
 * Shown only while the server has something selected and has said how healthy it is, read every frame like the character's own bars.
 */
UCLASS()
class ALVERION_API UTargetFrameWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UWidget> _frame;

	UPROPERTY()
	TObjectPtr<UTextBlock> _name;

	UPROPERTY()
	TObjectPtr<UProgressBar> _bar;

	// What the frame shows now, so the widgets are only touched when something changes. Zero and -1 until the first target.
	uint32 _shownTarget = 0;
	int32 _shownCurrent = -1;
	int32 _shownMaximum = -1;
};
