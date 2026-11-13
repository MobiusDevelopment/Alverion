#include "UI/ItemSlotWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "GameServer/WorldFlow.h"
#include "Items/ItemCatalog.h"
#include "ServerConnectionSubsystem.h"
#include "UI/InventoryLayout.h"
#include "UI/LocalizedText.h"
#include "UI/UIStyle.h"
#include "UI/UISubsystem.h"

namespace
{
	constexpr float itemSlotRadius = 4.0f;
	constexpr float itemSlotPadding = 3.0f;
	constexpr int32 itemSlotNameFontSize = 10;
	constexpr int32 itemSlotCountFontSize = 11;
	constexpr int32 itemSlotCaptionFontSize = 11;
	constexpr FLinearColor itemSlotEmptyFill = FLinearColor(0.02f, 0.018f, 0.016f, 0.9f);

	// A tile colour for each of the server's broad item kinds until items have pictures: weapon, armor, jewelry, quest item, adena, anything else.
	const FLinearColor itemSlotKindFills[] =
	{
		FLinearColor(0.32f, 0.1f, 0.08f, 1.0f),
		FLinearColor(0.1f, 0.17f, 0.3f, 1.0f),
		FLinearColor(0.26f, 0.12f, 0.3f, 1.0f),
		FLinearColor(0.28f, 0.26f, 0.08f, 1.0f),
		FLinearColor(0.4f, 0.31f, 0.06f, 1.0f),
		FLinearColor(0.16f, 0.16f, 0.15f, 1.0f)
	};

	// A shortcut whose item is gone, or one of a kind this client has nothing to use by yet.
	constexpr FLinearColor itemSlotDormantFill = FLinearColor(0.08f, 0.07f, 0.06f, 1.0f);

	// The bin's picture, loaded quietly since the bin shows its name instead until the picture exists.
	const TCHAR* const itemSlotBinIconPath = TEXT("/Game/UI/Icons/T_Icon_Bin.T_Icon_Bin");

	// The letters a shortcut of each kind shows until it has a picture, by the server's numbering.
	const TCHAR* const itemSlotShortcutLetters[] = { TEXT(""), TEXT(""), TEXT("S"), TEXT("A"), TEXT("M"), TEXT("R"), TEXT("B") };
}

void UItemSlotWidget::setup(EItemSlotKind kind, int32 index, const FText& caption, float size)
{
	_kind = kind;
	_index = index;
	_captionText = caption;
	_size = size;
}

void UItemSlotWidget::show(const Network::FInventoryItem* item, const Network::FShortcut* shortcut)
{
	_hasItem = (item != nullptr);
	_item = _hasItem ? *item : Network::FInventoryItem();
	_hasShortcut = (shortcut != nullptr);
	_shortcut = _hasShortcut ? *shortcut : Network::FShortcut();
	refreshLook();
}

void UItemSlotWidget::setCaption(const FText& caption)
{
	_captionText = caption;
	if (_caption != nullptr)
	{
		_caption->SetText(caption);
	}
}

TSharedRef<SWidget> UItemSlotWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		USizeBox* box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		box->SetWidthOverride(_size);
		box->SetHeightOverride(_size);

		_frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		_frame->SetPadding(FMargin(itemSlotPadding));
		box->AddChild(_frame);

		UOverlay* layers = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		_frame->SetContent(layers);

		// The item's picture fills the tile beneath the text.
		_icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		UOverlaySlot* iconSlot = layers->AddChildToOverlay(_icon);
		iconSlot->SetHorizontalAlignment(HAlign_Fill);
		iconSlot->SetVerticalAlignment(VAlign_Fill);

		_name = UIStyle::makeLabel(*WidgetTree, FText::GetEmpty(), itemSlotNameFontSize, UIStyle::labelColor);
		_name->SetAutoWrapText(true);
		_name->SetJustification(ETextJustify::Center);
		UOverlaySlot* nameSlot = layers->AddChildToOverlay(_name);
		nameSlot->SetHorizontalAlignment(HAlign_Fill);
		nameSlot->SetVerticalAlignment(VAlign_Center);

		_count = UIStyle::makeLabel(*WidgetTree, FText::GetEmpty(), itemSlotCountFontSize, UIStyle::labelColor);
		UOverlaySlot* countSlot = layers->AddChildToOverlay(_count);
		countSlot->SetHorizontalAlignment(HAlign_Right);
		countSlot->SetVerticalAlignment(VAlign_Bottom);

		// The bar's key sits in the corner whatever the slot holds, and a paperdoll slot's name sits along the bottom only while it is empty.
		_caption = UIStyle::makeLabel(*WidgetTree, _captionText, itemSlotCaptionFontSize, UIStyle::mutedColor);
		_caption->SetJustification(ETextJustify::Center);
		UOverlaySlot* captionSlot = layers->AddChildToOverlay(_caption);
		captionSlot->SetHorizontalAlignment((_kind == EItemSlotKind::Bar) ? HAlign_Left : HAlign_Center);
		captionSlot->SetVerticalAlignment((_kind == EItemSlotKind::Bar) ? VAlign_Top : VAlign_Bottom);

		WidgetTree->RootWidget = box;
		refreshLook();
	}

	return Super::RebuildWidget();
}

void UItemSlotWidget::refreshLook()
{
	if (_frame == nullptr)
	{
		return;
	}

	if (_kind == EItemSlotKind::Bin)
	{
		UTexture2D* binIcon = LoadObject<UTexture2D>(nullptr, itemSlotBinIconPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (binIcon != nullptr)
		{
			_icon->SetBrushFromTexture(binIcon);
		}

		_icon->SetVisibility((binIcon != nullptr) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		_frame->SetBrush(UIStyle::stateBrush(itemSlotEmptyFill, UIStyle::outline, itemSlotRadius));
		_name->SetText((binIcon != nullptr) ? FText::GetEmpty() : LocalizedText::get(TEXT("inventory.bin")));
		_count->SetText(FText::GetEmpty());
		_caption->SetVisibility(ESlateVisibility::Collapsed);
		SetToolTipText(LocalizedText::get(TEXT("inventory.bin_tooltip")));
		return;
	}

	FLinearColor fill = itemSlotEmptyFill;
	FText name = FText::GetEmpty();
	FText count = FText::GetEmpty();
	if (_hasItem)
	{
		fill = itemSlotKindFills[FMath::Min<int32>(_item.type2, UE_ARRAY_COUNT(itemSlotKindFills) - 1)];
		name = ItemCatalog::name(_item.itemId);
		if (_item.enchant > 0)
		{
			name = FText::Format(INVTEXT("+{0} {1}"), FText::AsNumber(_item.enchant), name);
		}

		if (_item.count > 1)
		{
			count = FText::AsNumber(_item.count);
		}
	}
	else if (_hasShortcut)
	{
		fill = itemSlotDormantFill;
		const int32 type = static_cast<int32>(_shortcut.type);
		name = FText::AsCultureInvariant((type >= 0) && (type < static_cast<int32>(UE_ARRAY_COUNT(itemSlotShortcutLetters))) ? itemSlotShortcutLetters[type] : TEXT(""));
	}

	// An item with a picture shows it in place of its name, which the tooltip still gives.
	const FItemData* data = _hasItem ? ItemCatalog::find(_item.itemId) : nullptr;
	UTexture2D* icon = ((data != nullptr) && !data->icon.IsEmpty()) ? LoadObject<UTexture2D>(nullptr, *data->icon) : nullptr;
	if (icon != nullptr)
	{
		_icon->SetBrushFromTexture(icon);
	}

	_icon->SetVisibility((icon != nullptr) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	// A picture shows as it was drawn, on the empty slot, without the kind's colour behind it.
	_frame->SetBrush(UIStyle::stateBrush((icon != nullptr) ? itemSlotEmptyFill : fill, UIStyle::outline, itemSlotRadius));
	_name->SetText((icon != nullptr) ? FText::GetEmpty() : name);
	_count->SetText(count);

	// The tooltip: the name with the count, then where it is worn and what it is, each line only when there is something to say.
	FString tooltip;
	if (_hasItem)
	{
		tooltip = count.IsEmpty() ? name.ToString() : FText::Format(INVTEXT("{0} ({1})"), name, count).ToString();
		for (const FText& line : { ItemCatalog::slotName(_item.itemId), ItemCatalog::description(_item.itemId) })
		{
			if (!line.IsEmpty())
			{
				tooltip += TEXT("\n") + line.ToString();
			}
		}
	}

	SetToolTipText(FText::FromString(tooltip));

	const bool captionShows = (_kind == EItemSlotKind::Bar) || !holdsSomething();
	_caption->SetVisibility(captionShows ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

bool UItemSlotWidget::holdsSomething() const
{
	return _hasItem || _hasShortcut;
}

Network::FWorldFlow* UItemSlotWidget::world() const
{
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* link = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	return ((link != nullptr) && link->world().isActive()) ? &link->world() : nullptr;
}

FReply UItemSlotWidget::NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
	if ((mouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) && holdsSomething())
	{
		return UWidgetBlueprintLibrary::DetectDragIfPressed(mouseEvent, this, EKeys::LeftMouseButton).NativeReply;
	}

	return Super::NativeOnMouseButtonDown(geometry, mouseEvent);
}

FReply UItemSlotWidget::NativeOnMouseButtonUp(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
	// A click on the bar that did not become a drag uses what the slot points at.
	Network::FWorldFlow* flow = world();
	if ((_kind == EItemSlotKind::Bar) && (mouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) && _hasShortcut && (flow != nullptr))
	{
		InventoryLayout::useShortcut(*flow, _index);
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonUp(geometry, mouseEvent);
}

FReply UItemSlotWidget::NativeOnMouseButtonDoubleClick(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
	Network::FWorldFlow* flow = world();
	if ((_kind != EItemSlotKind::Bar) && (mouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) && _hasItem && (flow != nullptr))
	{
		flow->useItem(_item.objectId);
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDoubleClick(geometry, mouseEvent);
}

void UItemSlotWidget::NativeOnDragDetected(const FGeometry& geometry, const FPointerEvent& mouseEvent, UDragDropOperation*& operation)
{
	UItemDragOperation* drag = NewObject<UItemDragOperation>(this);
	drag->sourceKind = _kind;
	drag->sourceIndex = _index;
	drag->objectId = ((_kind != EItemSlotKind::Bar) && _hasItem) ? _item.objectId : 0;
	drag->shortcut = _shortcut;

	// A copy of the slot follows the cursor, without its caption.
	UItemSlotWidget* visual = CreateWidget<UItemSlotWidget>(GetOwningPlayer(), UItemSlotWidget::StaticClass());
	visual->setup(EItemSlotKind::Bag, _index, FText::GetEmpty(), _size);
	visual->show(_hasItem ? &_item : nullptr, _hasShortcut ? &_shortcut : nullptr);
	drag->DefaultDragVisual = visual;
	drag->Pivot = EDragPivot::CenterCenter;
	operation = drag;
}

bool UItemSlotWidget::NativeOnDrop(const FGeometry& geometry, const FDragDropEvent& dragDropEvent, UDragDropOperation* operation)
{
	const UItemDragOperation* drag = Cast<UItemDragOperation>(operation);
	Network::FWorldFlow* flow = world();
	if ((drag == nullptr) || (flow == nullptr))
	{
		return false;
	}

	const bool sameSlot = (drag->sourceKind == _kind) && (drag->sourceIndex == _index);
	if (sameSlot)
	{
		return true;
	}

	// Only an item goes in the bin, never a shortcut, and only once the player says so. The server keeps what cannot be destroyed.
	if (_kind == EItemSlotKind::Bin)
	{
		if ((drag->sourceKind == EItemSlotKind::Bar) || (drag->objectId == 0))
		{
			return false;
		}

		confirmDestroy(drag->objectId);
		return true;
	}

	if (_kind == EItemSlotKind::Bar)
	{
		// A shortcut moved along the bar swaps with whatever the slot held; an item dropped here gets a new shortcut.
		if (drag->sourceKind == EItemSlotKind::Bar)
		{
			Network::FShortcut moved = drag->shortcut;
			moved.slot = _index;
			if (_hasShortcut)
			{
				Network::FShortcut displaced = _shortcut;
				displaced.slot = drag->sourceIndex;
				flow->setShortcut(displaced);
			}
			else
			{
				flow->clearShortcut(drag->sourceIndex);
			}

			flow->setShortcut(moved);
			return true;
		}

		Network::FShortcut shortcut;
		shortcut.slot = _index;
		shortcut.type = Network::EShortcutType::Item;
		shortcut.id = static_cast<int32>(drag->objectId);
		flow->setShortcut(shortcut);
		return true;
	}

	if (_kind == EItemSlotKind::Paperdoll)
	{
		// The server knows which slot an item goes on, so any paperdoll slot will do.
		if (drag->sourceKind != EItemSlotKind::Bag)
		{
			return false;
		}

		flow->useItem(drag->objectId);
		return true;
	}

	if (drag->sourceKind == EItemSlotKind::Paperdoll)
	{
		flow->unequip(InventoryLayout::unequipMask(drag->sourceIndex));
	}
	else if (drag->sourceKind == EItemSlotKind::Bag)
	{
		flow->saveInventoryOrder(InventoryLayout::moveInBag(flow->items(), drag->objectId, _index));
	}
	else
	{
		flow->clearShortcut(drag->sourceIndex);
	}

	return true;
}

void UItemSlotWidget::confirmDestroy(uint32 objectId)
{
	Network::FWorldFlow* flow = world();
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* ui = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	if ((flow == nullptr) || (ui == nullptr))
	{
		return;
	}

	const Network::FInventoryItem* item = flow->items().FindByPredicate([objectId](const Network::FInventoryItem& candidate)
	{
		return candidate.objectId == objectId;
	});

	if (item == nullptr)
	{
		return;
	}

	// The whole stack goes, so the question names how many.
	const int64 count = item->count;
	const FText name = (count > 1) ? FText::Format(INVTEXT("{0} ({1})"), ItemCatalog::name(item->itemId), FText::AsNumber(count)) : ItemCatalog::name(item->itemId);
	ui->askConfirm(FText::Format(LocalizedText::get(TEXT("inventory.destroy_confirm")), name), FOnConfirmAnswered::CreateWeakLambda(this, [this, objectId, count](bool confirmed)
	{
		Network::FWorldFlow* answerFlow = world();
		if (confirmed && (answerFlow != nullptr))
		{
			answerFlow->destroyItem(objectId, count);
		}
	}));
}

void UItemSlotWidget::NativeOnDragCancelled(const FDragDropEvent& dragDropEvent, UDragDropOperation* operation)
{
	// A shortcut dropped anywhere but a slot is taken off the bar.
	const UItemDragOperation* drag = Cast<UItemDragOperation>(operation);
	Network::FWorldFlow* flow = world();
	if ((drag != nullptr) && (drag->sourceKind == EItemSlotKind::Bar) && (flow != nullptr))
	{
		flow->clearShortcut(drag->sourceIndex);
	}

	Super::NativeOnDragCancelled(dragDropEvent, operation);
}
