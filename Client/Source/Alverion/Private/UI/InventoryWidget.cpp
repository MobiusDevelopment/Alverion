#include "UI/InventoryWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EnhancedInputSubsystems.h"
#include "GameServer/WorldFlow.h"
#include "InputAction.h"
#include "ServerConnectionSubsystem.h"
#include "UI/InventoryLayout.h"
#include "UI/ItemSlotWidget.h"
#include "UI/LocalizedText.h"
#include "UI/UIStyle.h"
#include "UI/UISubsystem.h"
#include "World/WorldCharacter.h"

namespace
{
	const TCHAR* const sheetInventoryActionPath = TEXT("/Game/Input/Actions/IA_Inventory.IA_Inventory");

	constexpr FLinearColor backdropColor = FLinearColor(0.01f, 0.01f, 0.015f, 0.55f);
	constexpr FLinearColor panelFill = FLinearColor(0.035f, 0.03f, 0.028f, 0.94f);
	constexpr FLinearColor headingColor = FLinearColor(0.86f, 0.68f, 0.36f, 1.0f);

	constexpr float panelRadius = 8.0f;
	constexpr float panelPadding = 20.0f;
	constexpr float panelGap = 16.0f;
	constexpr float bagSlotSize = 56.0f;
	constexpr float bagSlotGap = 6.0f;
	constexpr float sheetPortraitWidth = 200.0f;
	constexpr float sheetPortraitHeight = 280.0f;
	constexpr float statColumnWidth = 190.0f;
	constexpr int32 bagColumns = 8;

	// The rows the bag shows before it scrolls.
	constexpr int32 bagVisibleRows = 9;

	constexpr int32 headingFontSize = 20;
	constexpr int32 nameFontSize = 26;
	constexpr int32 smallFontSize = 14;
	constexpr int32 statFontSize = 15;

	// The figures under the picture, as two columns read top to bottom. refresh fills the values in this same order.
	const TCHAR* const statKeys[] =
	{
		TEXT("inventory.stat_hp"), TEXT("inventory.stat_mp"), TEXT("inventory.stat_strength"), TEXT("inventory.stat_dexterity"),
		TEXT("inventory.stat_constitution"), TEXT("inventory.stat_intelligence"), TEXT("inventory.stat_wit"), TEXT("inventory.stat_spirit"),
		TEXT("inventory.stat_attack"), TEXT("inventory.stat_defense"), TEXT("inventory.stat_magic_attack"), TEXT("inventory.stat_magic_defense"),
		TEXT("inventory.stat_accuracy"), TEXT("inventory.stat_evasion"), TEXT("inventory.stat_critical"), TEXT("inventory.stat_speed")
	};
	constexpr int32 statRowsPerColumn = 8;

	// A slot of the paperdoll or the bag.
	UItemSlotWidget* makeItemSlot(UWidgetTree& tree, EItemSlotKind kind, int32 index, const FText& caption)
	{
		UItemSlotWidget* slot = tree.ConstructWidget<UItemSlotWidget>(UItemSlotWidget::StaticClass());
		slot->setup(kind, index, caption, bagSlotSize);
		return slot;
	}

	// The rounded dark box a panel sits in, holding a column that starts with the heading.
	UVerticalBox* makePanel(UWidgetTree& tree, UHorizontalBox& row, UTextBlock& heading)
	{
		UBorder* panel = tree.ConstructWidget<UBorder>(UBorder::StaticClass());
		panel->SetBrush(UIStyle::stateBrush(panelFill, UIStyle::outline, panelRadius));
		panel->SetPadding(FMargin(panelPadding));

		UVerticalBox* column = tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		panel->SetContent(column);

		UVerticalBoxSlot* headingSlot = column->AddChildToVerticalBox(&heading);
		headingSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));

		// Both panels stretch to the taller one, so their bottom edges line up.
		UHorizontalBoxSlot* panelSlot = row.AddChildToHorizontalBox(panel);
		panelSlot->SetVerticalAlignment(VAlign_Fill);
		panelSlot->SetPadding(FMargin(panelGap * 0.5f, 0.0f));

		return column;
	}
}

TSharedRef<SWidget> UInventoryWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		// A faint dim over the running world, with both panels centred and shrunk to fit a small window.
		// Neither the dim nor the space around the panels takes the mouse, so an item can be dropped on the action bar beneath.
		UOverlay* root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		UImage* backdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush backdropBrush;
		backdropBrush.TintColor = FSlateColor(backdropColor);
		backdrop->SetBrush(backdropBrush);
		backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);

		UOverlaySlot* backdropSlot = root->AddChildToOverlay(backdrop);
		backdropSlot->SetHorizontalAlignment(HAlign_Fill);
		backdropSlot->SetVerticalAlignment(VAlign_Fill);

		UScaleBox* fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass());
		fit->SetStretch(EStretch::ScaleToFit);
		fit->SetStretchDirection(EStretchDirection::DownOnly);

		UOverlaySlot* fitSlot = root->AddChildToOverlay(fit);
		fitSlot->SetHorizontalAlignment(HAlign_Center);
		fitSlot->SetVerticalAlignment(VAlign_Center);
		fitSlot->SetPadding(FMargin(panelGap));

		UHorizontalBox* panels = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		fit->AddChild(panels);

		// The character: heading, name, the sex and level line, the equipment around the picture, and the figures under it.
		UVerticalBox* character = makePanel(*WidgetTree, *panels, *makeCaption(TEXT("inventory.character"), headingFontSize, headingColor));

		_nameText = UIStyle::makeLabel(*WidgetTree, FText::GetEmpty(), nameFontSize, UIStyle::frontEndTitleColor);
		character->AddChildToVerticalBox(_nameText);

		_summaryText = UIStyle::makeLabel(*WidgetTree, FText::GetEmpty(), smallFontSize, UIStyle::mutedColor);
		UVerticalBoxSlot* summarySlot = character->AddChildToVerticalBox(_summaryText);
		summarySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));

		UHorizontalBox* paperdoll = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		character->AddChildToVerticalBox(paperdoll);

		// The paperdoll in InventoryLayout's order: a column left of the picture, a column right of it, then the jewelry in a row underneath.
		const TConstArrayView<InventoryLayout::FPaperdollSlot> places = InventoryLayout::paperdollSlots();
		for (const InventoryLayout::FPaperdollSlot& place : places)
		{
			_paperdoll.Add(makeItemSlot(*WidgetTree, EItemSlotKind::Paperdoll, place.location, LocalizedText::get(place.captionKey)));
		}

		UVerticalBox* leftSlots = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		for (int32 index = 0; index < InventoryLayout::sideSlotCount; index++)
		{
			UVerticalBoxSlot* slot = leftSlots->AddChildToVerticalBox(_paperdoll[index]);
			slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, bagSlotGap));
		}

		paperdoll->AddChildToHorizontalBox(leftSlots);

		USizeBox* portraitBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		portraitBox->SetWidthOverride(sheetPortraitWidth);
		portraitBox->SetHeightOverride(sheetPortraitHeight);

		_portrait = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		portraitBox->AddChild(_portrait);

		UHorizontalBoxSlot* portraitSlot = paperdoll->AddChildToHorizontalBox(portraitBox);
		portraitSlot->SetPadding(FMargin(bagSlotGap * 2.0f, 0.0f));
		portraitSlot->SetVerticalAlignment(VAlign_Top);

		UVerticalBox* rightSlots = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		for (int32 index = InventoryLayout::sideSlotCount; index < (InventoryLayout::sideSlotCount * 2); index++)
		{
			UVerticalBoxSlot* slot = rightSlots->AddChildToVerticalBox(_paperdoll[index]);
			slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, bagSlotGap));
		}

		paperdoll->AddChildToHorizontalBox(rightSlots);

		UHorizontalBox* jewelry = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		for (int32 index = InventoryLayout::sideSlotCount * 2; index < _paperdoll.Num(); index++)
		{
			UHorizontalBoxSlot* slot = jewelry->AddChildToHorizontalBox(_paperdoll[index]);
			slot->SetPadding(FMargin(bagSlotGap * 0.5f, 0.0f));
		}

		UVerticalBoxSlot* jewelrySlot = character->AddChildToVerticalBox(jewelry);
		jewelrySlot->SetHorizontalAlignment(HAlign_Center);
		jewelrySlot->SetPadding(FMargin(0.0f, bagSlotGap, 0.0f, 0.0f));

		UHorizontalBox* stats = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UVerticalBoxSlot* statsSlot = character->AddChildToVerticalBox(stats);
		statsSlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 0.0f));

		UVerticalBox* statColumn = nullptr;
		for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(statKeys)); index++)
		{
			if ((index % statRowsPerColumn) == 0)
			{
				USizeBox* columnBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				columnBox->SetWidthOverride(statColumnWidth);
				statColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
				columnBox->AddChild(statColumn);

				UHorizontalBoxSlot* columnSlot = stats->AddChildToHorizontalBox(columnBox);
				columnSlot->SetPadding(FMargin(0.0f, 0.0f, (index == 0) ? panelGap : 0.0f, 0.0f));
			}

			// The label to the left and the figure pushed to the right edge of its column.
			UHorizontalBox* row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			UHorizontalBoxSlot* labelSlot = row->AddChildToHorizontalBox(makeCaption(statKeys[index], statFontSize, UIStyle::mutedColor));
			labelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

			UTextBlock* value = UIStyle::makeLabel(*WidgetTree, FText::GetEmpty(), statFontSize, UIStyle::labelColor);
			row->AddChildToHorizontalBox(value);
			_statValues.Add(value);

			UVerticalBoxSlot* rowSlot = statColumn->AddChildToVerticalBox(row);
			rowSlot->SetPadding(FMargin(0.0f, 1.0f));
		}

		// The bag: a grid as big as the server says the bag is, scrolling past the rows that fit. refreshItems fills it.
		UVerticalBox* bag = makePanel(*WidgetTree, *panels, *makeCaption(TEXT("inventory.title"), headingFontSize, headingColor));

		USizeBox* bagView = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		bagView->SetHeightOverride(bagVisibleRows * (bagSlotSize + bagSlotGap));

		UScrollBox* scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		bagView->AddChild(scroll);

		_bagGrid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass());
		_bagGrid->SetSlotPadding(FMargin(bagSlotGap * 0.5f));
		scroll->AddChild(_bagGrid);

		bag->AddChildToVerticalBox(bagView);

		WidgetTree->RootWidget = root;

		// Focusable, so the keys reach it once CommonUI puts the focus here.
		SetIsFocusable(true);
		_inventoryAction = LoadObject<UInputAction>(nullptr, sheetInventoryActionPath);
	}

	return Super::RebuildWidget();
}

UTextBlock* UInventoryWidget::makeCaption(const TCHAR* key, int32 size, const FLinearColor& colour)
{
	UTextBlock* caption = UIStyle::makeLabel(*WidgetTree, LocalizedText::get(key), size, colour);
	_captions.Add(caption);
	_captionKeys.Add(key);
	return caption;
}

void UInventoryWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	refresh();

	// The picture films only while the sheet is up.
	AWorldCharacter* character = Cast<AWorldCharacter>(GetOwningPlayerPawn());
	if ((character != nullptr) && (_portrait != nullptr))
	{
		_portrait->SetBrushResourceObject(character->beginPortrait());
	}
}

void UInventoryWidget::NativeOnDeactivated()
{
	AWorldCharacter* character = Cast<AWorldCharacter>(GetOwningPlayerPawn());
	if (character != nullptr)
	{
		character->endPortrait();
	}

	Super::NativeOnDeactivated();
}

UWidget* UInventoryWidget::NativeGetDesiredFocusTarget() const
{
	// Nothing on the sheet takes input yet, so the sheet itself holds the focus the keys arrive through.
	return const_cast<UInventoryWidget*>(this);
}

FReply UInventoryWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
	const FKey key = keyEvent.GetKey();
	if (key == EKeys::Escape)
	{
		UGameInstance* gameInstance = GetGameInstance();
		UUISubsystem* ui = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
		if ((ui == nullptr) || ui->acceptEscape(keyEvent.IsRepeat()))
		{
			DeactivateWidget();
		}

		return FReply::Handled();
	}

	ULocalPlayer* localPlayer = GetOwningLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* input = (localPlayer != nullptr) ? localPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!keyEvent.IsRepeat() && (input != nullptr) && (_inventoryAction != nullptr) && input->QueryKeysMappedToAction(_inventoryAction).Contains(key))
	{
		DeactivateWidget();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(geometry, keyEvent);
}

void UInventoryWidget::refresh()
{
	for (int32 index = 0; index < _captions.Num(); index++)
	{
		_captions[index]->SetText(LocalizedText::get(_captionKeys[index]));
	}

	const TConstArrayView<InventoryLayout::FPaperdollSlot> places = InventoryLayout::paperdollSlots();
	for (int32 index = 0; (index < _paperdoll.Num()) && (index < places.Num()); index++)
	{
		_paperdoll[index]->setCaption(LocalizedText::get(places[index].captionKey));
	}

	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* link = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (link == nullptr)
	{
		return;
	}

	const Network::FCharacterSelected& selected = link->gameServer().selected();
	const Network::FUserInfo& info = link->world().userInfo();

	// UserInfo follows the character as it levels; the selection is all there is before the first one arrives.
	const int32 level = (info.level > 0) ? info.level : selected.level;
	const FText sex = LocalizedText::get(selected.female ? TEXT("charcreate.sex_female") : TEXT("charcreate.sex_male"));

	_nameText->SetText(FText::FromString(selected.name));
	_summaryText->SetText(FText::Format(LocalizedText::get(TEXT("inventory.summary")), sex, FText::AsNumber(level)));

	const FText pairFormat = INVTEXT("{0} / {1}");
	const FText values[] =
	{
		FText::Format(pairFormat, FText::AsNumber(info.currentHp), FText::AsNumber(info.maxHp)),
		FText::Format(pairFormat, FText::AsNumber(info.currentMp), FText::AsNumber(info.maxMp)),
		FText::AsNumber(info.strength),
		FText::AsNumber(info.dexterity),
		FText::AsNumber(info.constitution),
		FText::AsNumber(info.intelligence),
		FText::AsNumber(info.wit),
		FText::AsNumber(info.spirit),
		FText::AsNumber(info.attack),
		FText::AsNumber(info.defense),
		FText::AsNumber(info.magicAttack),
		FText::AsNumber(info.magicDefense),
		FText::AsNumber(info.accuracy),
		FText::AsNumber(info.evasion),
		FText::AsNumber(info.critical),
		FText::AsNumber(info.runSpeed)
	};

	for (int32 index = 0; (index < _statValues.Num()) && (index < static_cast<int32>(UE_ARRAY_COUNT(values))); index++)
	{
		_statValues[index]->SetText(values[index]);
	}

	refreshItems();
}

void UInventoryWidget::NativeTick(const FGeometry& geometry, float deltaSeconds)
{
	Super::NativeTick(geometry, deltaSeconds);

	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* link = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if ((link != nullptr) && (link->world().revision() != _shownRevision))
	{
		refreshItems();
	}
}

void UInventoryWidget::refreshItems()
{
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* link = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if ((link == nullptr) || (_bagGrid == nullptr))
	{
		return;
	}

	const Network::FWorldFlow& world = link->world();
	_shownRevision = world.revision();
	const TArray<Network::FInventoryItem>& items = world.items();

	const TConstArrayView<InventoryLayout::FPaperdollSlot> places = InventoryLayout::paperdollSlots();
	for (int32 index = 0; (index < _paperdoll.Num()) && (index < places.Num()); index++)
	{
		_paperdoll[index]->show(InventoryLayout::wornAt(items, places[index].location), nullptr);
	}

	// Whole rows, at least as many places as the bag holds and never fewer than what is in it.
	const TArray<const Network::FInventoryItem*> bagItems = InventoryLayout::bagItems(items);
	const int32 limit = (world.userInfo().inventoryLimit > 0) ? world.userInfo().inventoryLimit : InventoryLayout::defaultBagSize;
	const int32 wanted = FMath::DivideAndRoundUp(FMath::Max(limit, bagItems.Num()), bagColumns) * bagColumns;
	while (_bag.Num() < wanted)
	{
		const int32 index = _bag.Num();
		UItemSlotWidget* slot = makeItemSlot(*WidgetTree, EItemSlotKind::Bag, index, FText::GetEmpty());
		_bagGrid->AddChildToUniformGrid(slot, index / bagColumns, index % bagColumns);
		_bag.Add(slot);
	}

	for (int32 index = 0; index < _bag.Num(); index++)
	{
		_bag[index]->show(bagItems.IsValidIndex(index) ? bagItems[index] : nullptr, nullptr);
	}
}
