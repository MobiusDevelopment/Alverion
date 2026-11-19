#include "UI/LootWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Input/CommonUIInputTypes.h"
#include "Items/ItemCatalog.h"
#include "ServerConnectionSubsystem.h"
#include "UI/LocalizedText.h"
#include "UI/UIStyle.h"

namespace
{
	constexpr float lootWindowWidth = 360.0f;
	constexpr float lootWindowPadding = 16.0f;
	constexpr float lootIconSize = 40.0f;
	constexpr float lootRowGap = 4.0f;
	constexpr int32 lootTitleFontSize = 18;
	constexpr int32 lootRowFontSize = 14;

	// The confirm dialog's panel, so the windows over the world read as one family.
	const FLinearColor lootPanelColor = FLinearColor::FromSRGBColor(FColor(84, 62, 47));

	// Left of centre, out of the way of the character standing over the corpse.
	const FMargin lootWindowOffset(0.0f, 0.0f, 520.0f, 0.0f);
}

void ULootRow::setup(ULootWidget* owner, int32 index, const Network::FLootStack& stack)
{
	_owner = owner;
	_index = index;
	_stack = stack;
}

TSharedRef<SWidget> ULootRow::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		UButton* button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		button->SetStyle(UIStyle::buttonStyle());
		button->OnClicked.AddUniqueDynamic(this, &ULootRow::handleClicked);

		UHorizontalBox* line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		// The item's picture where it has one, an empty square where it has none, so the names line up.
		USizeBox* iconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		iconBox->SetWidthOverride(lootIconSize);
		iconBox->SetHeightOverride(lootIconSize);
		const FItemData* data = ItemCatalog::find(_stack.itemId);
		UTexture2D* icon = ((data != nullptr) && !data->icon.IsEmpty()) ? LoadObject<UTexture2D>(nullptr, *data->icon) : nullptr;
		if (icon != nullptr)
		{
			UImage* picture = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			picture->SetBrushFromTexture(icon);
			iconBox->AddChild(picture);
		}

		line->AddChildToHorizontalBox(iconBox)->SetVerticalAlignment(VAlign_Center);

		const FText name = (_stack.count > 1) ? FText::Format(INVTEXT("{0} ({1})"), ItemCatalog::name(_stack.itemId), FText::AsNumber(_stack.count)) : ItemCatalog::name(_stack.itemId);
		UTextBlock* label = UIStyle::makeLabel(*WidgetTree, name, lootRowFontSize, UIStyle::labelColor);
		UHorizontalBoxSlot* labelSlot = line->AddChildToHorizontalBox(label);
		labelSlot->SetVerticalAlignment(VAlign_Center);
		labelSlot->SetPadding(FMargin(lootRowGap * 2.0f, 0.0f, 0.0f, 0.0f));

		button->AddChild(line);
		WidgetTree->RootWidget = button;
	}

	return Super::RebuildWidget();
}

void ULootRow::handleClicked()
{
	if (_owner != nullptr)
	{
		_owner->takeStack(_index, _stack.itemId);
	}
}

void ULootWidget::show(const Network::FCorpseLoot& loot, const FText& corpseName)
{
	_loot = loot;
	_corpseName = corpseName;
	refreshRows();
}

uint32 ULootWidget::corpseId() const
{
	return _loot.corpseId;
}

void ULootWidget::takeStack(int32 index, int32 itemId)
{
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* link = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (link != nullptr)
	{
		link->world().takeLoot(_loot.corpseId, index, itemId);
	}
}

TSharedRef<SWidget> ULootWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		UBorder* root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		root->SetBrushColor(FLinearColor::Transparent);
		root->SetHorizontalAlignment(HAlign_Center);
		root->SetVerticalAlignment(VAlign_Center);
		root->SetPadding(lootWindowOffset);

		USizeBox* panel = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		panel->SetWidthOverride(lootWindowWidth);

		UBorder* background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		background->SetBrush(UIStyle::roundedBrush(lootPanelColor));
		background->SetPadding(FMargin(lootWindowPadding));

		UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		_title = UIStyle::makeLabel(*WidgetTree, FText::GetEmpty(), lootTitleFontSize, UIStyle::labelColor);
		_title->SetJustification(ETextJustify::Center);
		column->AddChildToVerticalBox(_title)->SetHorizontalAlignment(HAlign_Center);

		_rows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UVerticalBoxSlot* rowsSlot = column->AddChildToVerticalBox(_rows);
		rowsSlot->SetHorizontalAlignment(HAlign_Fill);
		rowsSlot->SetPadding(FMargin(0.0f, lootWindowPadding * 0.5f, 0.0f, lootWindowPadding * 0.5f));

		_takeAllButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		_takeAllButton->SetStyle(UIStyle::buttonStyle());
		UTextBlock* takeAll = UIStyle::makeLabel(*WidgetTree, LocalizedText::get(TEXT("loot.take_all")), UIStyle::bodyFontSize, UIStyle::labelColor);
		takeAll->SetJustification(ETextJustify::Center);
		_takeAllButton->AddChild(takeAll);
		column->AddChildToVerticalBox(_takeAllButton)->SetHorizontalAlignment(HAlign_Center);

		background->AddChild(column);
		panel->AddChild(background);
		root->AddChild(panel);
		WidgetTree->RootWidget = root;
		refreshRows();
	}

	return Super::RebuildWidget();
}

void ULootWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Unique, because the stack can hand back the same instance for the next corpse.
	if (_takeAllButton != nullptr)
	{
		_takeAllButton->OnClicked.AddUniqueDynamic(this, &ULootWidget::handleTakeAllClicked);
	}

	SetIsFocusable(true);
}

TOptional<FUIInputConfig> ULootWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
}

void ULootWidget::handleTakeAllClicked()
{
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* link = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (link != nullptr)
	{
		link->world().takeAllLoot(_loot.corpseId);
	}
}

void ULootWidget::refreshRows()
{
	if ((_rows == nullptr) || (_title == nullptr))
	{
		return;
	}

	_title->SetText(_corpseName);
	_rows->ClearChildren();
	for (int32 index = 0; index < _loot.stacks.Num(); index++)
	{
		ULootRow* row = CreateWidget<ULootRow>(this, ULootRow::StaticClass());
		row->setup(this, index, _loot.stacks[index]);
		UVerticalBoxSlot* rowSlot = _rows->AddChildToVerticalBox(row);
		rowSlot->SetHorizontalAlignment(HAlign_Fill);
		rowSlot->SetPadding(FMargin(0.0f, lootRowGap * 0.5f));
	}
}
