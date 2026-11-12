#pragma once

#include "CommonActivatableWidget.h"
#include "CoreMinimal.h"

#include "InventoryWidget.generated.h"

class UImage;
class UInputAction;
class UItemSlotWidget;
class UTextBlock;
class UUniformGridPanel;

/**
 * The character sheet and the bag, opened over the running world by the inventory key and closed by it or Escape.
 * The paperdoll and the bag show what the server says the character carries, and items move between them and onto the action bar by dragging.
 * The figures come from the server's latest UserInfo, and the picture is the character filmed live from the front.
 */
UCLASS()
class ALVERION_API UInventoryWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

protected:
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

	/** Escape, or the inventory key wherever it was rebound, closes the sheet: CommonUI holds the game's keys back while it is up. */
	virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

private:
	/** Makes a label whose text is the string table entry for the key, and remembers it so refresh can follow a language change. */
	UTextBlock* makeCaption(const TCHAR* key, int32 size, const FLinearColor& colour);

	/** Sets every caption in the current language, and the name, the level line and every figure from what the server last said. */
	void refresh();

	/** Puts what the character carries in the paperdoll and the bag, growing the bag to what the server says it holds. */
	void refreshItems();

	/** The paperdoll in InventoryLayout's order. */
	UPROPERTY()
	TArray<TObjectPtr<UItemSlotWidget>> _paperdoll;

	UPROPERTY()
	TArray<TObjectPtr<UItemSlotWidget>> _bag;

	UPROPERTY()
	TObjectPtr<UUniformGridPanel> _bagGrid;

	/** The world's revision the slots last showed, so they are filled again only when something changed. */
	uint32 _shownRevision = 0;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> _captions;

	TArray<FString> _captionKeys;

	UPROPERTY()
	TObjectPtr<UTextBlock> _nameText;

	UPROPERTY()
	TObjectPtr<UTextBlock> _summaryText;

	UPROPERTY()
	TObjectPtr<UImage> _portrait;

	/** One per stat row, in the order the rows are built. */
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> _statValues;

	UPROPERTY()
	TObjectPtr<UInputAction> _inventoryAction;
};
