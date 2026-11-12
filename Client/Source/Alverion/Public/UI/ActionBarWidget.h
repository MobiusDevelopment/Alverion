#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "ActionBarWidget.generated.h"

class UItemSlotWidget;

/**
 * The row of ten shortcut slots along the bottom of the world screen, the first ten of the server's first page.
 * Each holds a shortcut rather than an item, captioned with the key that uses it: items are dragged on from the bag or the paperdoll, dragged along to move, and dragged off to remove.
 * The slots take the mouse only while a screen over the world shows the cursor, so play underneath is never blocked.
 */
UCLASS()
class ALVERION_API UActionBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** How many slots the bar has. */
	static constexpr int32 slotCount = 10;

	virtual TSharedRef<SWidget> RebuildWidget() override;

protected:
	virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

private:
	/** Shows each slot's shortcut, and the item it points at while the character still has it. */
	void refreshSlots();

	/** Captions each slot with the key bound to it now, since keys can be rebound while the bar is up. */
	void refreshKeys();

	UPROPERTY()
	TArray<TObjectPtr<UItemSlotWidget>> _slots;

	uint32 _shownRevision = 0;
	float _sinceKeysRefreshed = 0.0f;
};
