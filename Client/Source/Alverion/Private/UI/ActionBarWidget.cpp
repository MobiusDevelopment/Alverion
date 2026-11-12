#include "UI/ActionBarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Core/InputBootstrapSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "GameServer/WorldFlow.h"
#include "ServerConnectionSubsystem.h"
#include "UI/ItemSlotWidget.h"

namespace
{
	constexpr float barSlotSize = 52.0f;
	constexpr float barSlotGap = 6.0f;
	constexpr float barBottomMargin = 24.0f;

	// How often the key captions are read again, cheap enough and quick enough to follow a rebind.
	constexpr float barKeysRefreshSeconds = 0.5f;
}

TSharedRef<SWidget> UActionBarWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		// Only the slots themselves take the mouse; the screen around them stays the world's.
		UOverlay* root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		UHorizontalBox* row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		row->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UOverlaySlot* rowSlot = root->AddChildToOverlay(row);
		rowSlot->SetHorizontalAlignment(HAlign_Center);
		rowSlot->SetVerticalAlignment(VAlign_Bottom);
		rowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, barBottomMargin));

		for (int32 index = 0; index < slotCount; index++)
		{
			// Captioned with the number keys until refreshKeys reads the real bindings.
			UItemSlotWidget* slot = WidgetTree->ConstructWidget<UItemSlotWidget>(UItemSlotWidget::StaticClass());
			slot->setup(EItemSlotKind::Bar, index, FText::AsCultureInvariant(FString::FromInt((index + 1) % slotCount)), barSlotSize);
			_slots.Add(slot);

			UHorizontalBoxSlot* boxSlot = row->AddChildToHorizontalBox(slot);
			boxSlot->SetPadding(FMargin(barSlotGap * 0.5f, 0.0f));
		}

		WidgetTree->RootWidget = root;
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	return Super::RebuildWidget();
}

void UActionBarWidget::NativeTick(const FGeometry& geometry, float deltaSeconds)
{
	Super::NativeTick(geometry, deltaSeconds);

	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* link = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if ((link != nullptr) && (link->world().revision() != _shownRevision))
	{
		refreshSlots();
	}

	_sinceKeysRefreshed += deltaSeconds;
	if (_sinceKeysRefreshed >= barKeysRefreshSeconds)
	{
		_sinceKeysRefreshed = 0.0f;
		refreshKeys();
	}
}

void UActionBarWidget::refreshSlots()
{
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* link = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (link == nullptr)
	{
		return;
	}

	const Network::FWorldFlow& world = link->world();
	_shownRevision = world.revision();
	for (int32 index = 0; index < _slots.Num(); index++)
	{
		const Network::FShortcut* shortcut = world.shortcuts().Find(index);
		const Network::FInventoryItem* item = nullptr;
		if ((shortcut != nullptr) && (shortcut->type == Network::EShortcutType::Item))
		{
			item = world.items().FindByPredicate([shortcut](const Network::FInventoryItem& candidate)
			{
				return candidate.objectId == static_cast<uint32>(shortcut->id);
			});
		}

		_slots[index]->show(item, shortcut);
	}
}

void UActionBarWidget::refreshKeys()
{
	ULocalPlayer* localPlayer = GetOwningLocalPlayer();
	const UInputBootstrapSubsystem* bootstrap = (localPlayer != nullptr) ? localPlayer->GetSubsystem<UInputBootstrapSubsystem>() : nullptr;
	const UEnhancedInputLocalPlayerSubsystem* input = (localPlayer != nullptr) ? localPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if ((bootstrap == nullptr) || (input == nullptr))
	{
		return;
	}

	for (int32 index = 0; index < _slots.Num(); index++)
	{
		const UInputAction* action = bootstrap->getShortcutAction(index);
		const TArray<FKey> keys = (action != nullptr) ? input->QueryKeysMappedToAction(action) : TArray<FKey>();
		const FKey* key = keys.FindByPredicate([](const FKey& candidate)
		{
			return !candidate.IsGamepadKey();
		});

		_slots[index]->setCaption((key != nullptr) ? key->GetDisplayName(false) : FText::GetEmpty());
	}
}
