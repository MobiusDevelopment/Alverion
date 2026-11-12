#pragma once

#include "Blueprint/DragDropOperation.h"
#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "GameServer/GameServerProtocol.h"

#include "ItemSlotWidget.generated.h"

class UBorder;
class UImage;
class UTextBlock;

namespace Network
{
	class FWorldFlow;
}

/** Which of the three places a slot belongs to, which decides what dropping something on it does. */
enum class EItemSlotKind : uint8
{
	Bag,
	Paperdoll,
	Bar
};

/** What is being dragged: where it came from and what it is. A shortcut drags as the shortcut, so moving it never moves the item. */
UCLASS()
class ALVERION_API UItemDragOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	EItemSlotKind sourceKind = EItemSlotKind::Bag;

	/** The place it came from: the bag place, the paperdoll place or the bar slot. */
	int32 sourceIndex = 0;

	/** The dragged item, or zero for a bar shortcut. */
	uint32 objectId = 0;

	Network::FShortcut shortcut;
};

/**
 * One slot of the bag, the paperdoll or the action bar, showing the item or the shortcut in it.
 * Items move by dragging: onto the paperdoll to wear, back to the bag to take off or to reorder, and onto the bar to make a shortcut.
 * A shortcut dragged off the bar is gone, and the item it pointed at stays where it was.
 * Double clicking an item uses it, which wears or takes off what can be worn, and clicking a bar slot uses what it points at.
 */
UCLASS()
class ALVERION_API UItemSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Which place the slot is, its index there, the caption it shows and its size. Called once, before the slot is built. */
	void setup(EItemSlotKind kind, int32 index, const FText& caption, float size);

	/** Shows an item, a shortcut or nothing. On the bar the shortcut is what the slot holds, and the item, while the character still has it, is what it points at. */
	void show(const Network::FInventoryItem* item, const Network::FShortcut* shortcut);

	/** The caption: the slot's name while the paperdoll slot is empty, and the key that uses it on the bar. */
	void setCaption(const FText& caption);

	virtual TSharedRef<SWidget> RebuildWidget() override;

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& geometry, const FPointerEvent& mouseEvent, UDragDropOperation*& operation) override;
	virtual bool NativeOnDrop(const FGeometry& geometry, const FDragDropEvent& dragDropEvent, UDragDropOperation* operation) override;
	virtual void NativeOnDragCancelled(const FDragDropEvent& dragDropEvent, UDragDropOperation* operation) override;

private:
	Network::FWorldFlow* world() const;

	/** Draws what show was last given, once the parts exist. */
	void refreshLook();

	/** Whether there is anything to drag or use. */
	bool holdsSomething() const;

	EItemSlotKind _kind = EItemSlotKind::Bag;
	int32 _index = 0;
	float _size = 56.0f;
	FText _captionText;

	Network::FInventoryItem _item;
	bool _hasItem = false;
	Network::FShortcut _shortcut;
	bool _hasShortcut = false;

	UPROPERTY()
	TObjectPtr<UBorder> _frame;

	UPROPERTY()
	TObjectPtr<UImage> _icon;

	UPROPERTY()
	TObjectPtr<UTextBlock> _name;

	UPROPERTY()
	TObjectPtr<UTextBlock> _count;

	UPROPERTY()
	TObjectPtr<UTextBlock> _caption;
};
