#include "Core/ClientPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "CommonActivatableWidget.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "UI/ActionBarWidget.h"
#include "UI/DeathMenuWidget.h"
#include "UI/GameMenuWidget.h"
#include "UI/LootWidget.h"
#include "UI/ChatLineWidget.h"
#include "UI/InventoryLayout.h"
#include "UI/InventoryWidget.h"
#include "UI/MapWidget.h"
#include "UI/NpcDialogWidget.h"
#include "InputAction.h"
#include "Core/InputBootstrapSubsystem.h"
#include "ServerConnectionSubsystem.h"
#include "UI/UISubsystem.h"
#include "World/WorldCharacter.h"
#include "World/WorldGameMode.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace
{
	const TCHAR* const menuActionPath = TEXT("/Game/Input/Actions/IA_Menu.IA_Menu");
	const TCHAR* const inventoryActionPath = TEXT("/Game/Input/Actions/IA_Inventory.IA_Inventory");
	const TCHAR* const mapScreenActionPath = TEXT("/Game/Input/Actions/IA_Map.IA_Map");
	const TCHAR* const chatActionPath = TEXT("/Game/Input/Actions/IA_Chat.IA_Chat");
	const TCHAR* const interactActionPath = TEXT("/Game/Input/Actions/IA_Interact.IA_Interact");

	// A dialog is closed once the character has walked a little past the range it was opened from, which is well inside the 250 units the server allows.
	constexpr double dialogLeaveRangeCm = 70.0 * Network::worldUnitCm;

	// How near a monster has to be, in server units, for a swing to reach it. The server allows the weapon's range, 40 for a sword and 20 bare-handed, and both bodies' widths besides.
	constexpr double attackArmedReachUnits = 50.0;
	constexpr double attackUnarmedReachUnits = 30.0;

	// The loot window closes once the character is this far from the corpse, still inside the 150 units the server loots within.
	constexpr double lootLeaveRangeCm = 120.0 * Network::worldUnitCm;
}

void AClientPlayerController::PlayerTick(float deltaSeconds)
{
	Super::PlayerTick(deltaSeconds);

	// The loot window closes once the character walks away from the corpse, or the corpse goes.
	if (_loot.IsValid() && _loot->IsActivated())
	{
		UWorld* lootWorld = GetWorld();
		const AWorldGameMode* lootMode = (lootWorld != nullptr) ? lootWorld->GetAuthGameMode<AWorldGameMode>() : nullptr;
		const APawn* looter = GetPawn();
		double lootDistance = 0.0;
		if ((lootMode != nullptr) && (looter != nullptr) && (!lootMode->distanceToCreature(_loot->corpseId(), *looter, lootDistance) || (lootDistance > lootLeaveRangeCm)))
		{
			_loot->DeactivateWidget();
		}
	}

	if (_dialogObjectId == 0)
	{
		return;
	}

	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	UCommonActivatableWidgetStack* layer = (subsystem != nullptr) ? subsystem->getLayer(EUILayer::GameMenu) : nullptr;
	UNpcDialogWidget* dialog = (layer != nullptr) ? Cast<UNpcDialogWidget>(layer->GetActiveWidget()) : nullptr;
	if (dialog == nullptr)
	{
		// Closed some other way, so there is nothing left to watch.
		_dialogObjectId = 0;
		return;
	}

	UWorld* world = GetWorld();
	AWorldGameMode* mode = (world != nullptr) ? world->GetAuthGameMode<AWorldGameMode>() : nullptr;
	APawn* character = GetPawn();
	double distance = 0.0;
	if ((mode != nullptr) && (character != nullptr) && mode->distanceToCreature(_dialogObjectId, *character, distance) && (distance > dialogLeaveRangeCm))
	{
		_dialogObjectId = 0;
		dialog->DeactivateWidget();
	}
}

void AClientPlayerController::BeginPlay()
{
	Super::BeginPlay();

	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* connection = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (connection != nullptr)
	{
		connection->world().onNpcHtml.BindUObject(this, &AClientPlayerController::showNpcHtml);
	}
}

void AClientPlayerController::EndPlay(const EEndPlayReason::Type reason)
{
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* connection = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if ((connection != nullptr) && connection->world().onNpcHtml.IsBoundToObject(this))
	{
		connection->world().onNpcHtml.Unbind();
	}

	Super::EndPlay(reason);
}

void AClientPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Enhanced Input keeps applied contexts on this controller's player input, and travel destroys the controller, so a level arrived at by travel starts with no keys until the context is applied again here.
	ULocalPlayer* localPlayer = GetLocalPlayer();
	UInputBootstrapSubsystem* bootstrap = (localPlayer != nullptr) ? localPlayer->GetSubsystem<UInputBootstrapSubsystem>() : nullptr;
	if (bootstrap != nullptr)
	{
		bootstrap->applyDefaultMappings();
	}

	UEnhancedInputComponent* enhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (enhancedInput == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("ClientPlayerController has no Enhanced Input component, so the menu, inventory and map keys do nothing."));
		return;
	}

	UInputAction* menuAction = LoadObject<UInputAction>(nullptr, menuActionPath);
	if (menuAction != nullptr)
	{
		enhancedInput->BindAction(menuAction, ETriggerEvent::Started, this, &AClientPlayerController::toggleGameMenu);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ClientPlayerController could not load %s, so the menu key does nothing."), menuActionPath);
	}

	UInputAction* inventoryAction = LoadObject<UInputAction>(nullptr, inventoryActionPath);
	if (inventoryAction != nullptr)
	{
		enhancedInput->BindAction(inventoryAction, ETriggerEvent::Started, this, &AClientPlayerController::toggleInventory);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ClientPlayerController could not load %s, so the inventory key does nothing."), inventoryActionPath);
	}

	UInputAction* mapAction = LoadObject<UInputAction>(nullptr, mapScreenActionPath);
	if (mapAction != nullptr)
	{
		enhancedInput->BindAction(mapAction, ETriggerEvent::Started, this, &AClientPlayerController::toggleMap);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ClientPlayerController could not load %s, so the map key does nothing."), mapScreenActionPath);
	}

	UInputAction* chatAction = LoadObject<UInputAction>(nullptr, chatActionPath);
	if (chatAction != nullptr)
	{
		enhancedInput->BindAction(chatAction, ETriggerEvent::Started, this, &AClientPlayerController::startChat);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ClientPlayerController could not load %s, so the chat key does nothing."), chatActionPath);
	}

	UInputAction* interactAction = LoadObject<UInputAction>(nullptr, interactActionPath);
	if (interactAction != nullptr)
	{
		enhancedInput->BindAction(interactAction, ETriggerEvent::Started, this, &AClientPlayerController::interact);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ClientPlayerController could not load %s, so the interact key does nothing."), interactActionPath);
	}

	const UInputAction* attackAction = (bootstrap != nullptr) ? bootstrap->getAttackAction() : nullptr;
	if (attackAction != nullptr)
	{
		enhancedInput->BindAction(attackAction, ETriggerEvent::Started, this, &AClientPlayerController::attack);
	}

	// The action bar's keys, each using what its slot points at.
	for (int32 index = 0; index < UActionBarWidget::slotCount; index++)
	{
		const UInputAction* shortcutAction = (bootstrap != nullptr) ? bootstrap->getShortcutAction(index) : nullptr;
		if (shortcutAction != nullptr)
		{
			enhancedInput->BindAction(shortcutAction, ETriggerEvent::Started, this, &AClientPlayerController::useShortcut, index);
		}
	}

	// Alt+H for a clean screenshot: a fixed chord made here rather than a rebindable action asset, read as H with Alt held.
	if (_hideInterfaceAction == nullptr)
	{
		_hideInterfaceAction = NewObject<UInputAction>(this);
		_hideInterfaceMapping = NewObject<UInputMappingContext>(this);
		_hideInterfaceMapping->MapKey(_hideInterfaceAction, EKeys::H);
	}

	UEnhancedInputLocalPlayerSubsystem* input = (localPlayer != nullptr) ? localPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (input != nullptr)
	{
		input->AddMappingContext(_hideInterfaceMapping, 1);
	}

	enhancedInput->BindAction(_hideInterfaceAction, ETriggerEvent::Started, this, &AClientPlayerController::toggleInterface);
}

void AClientPlayerController::toggleInterface()
{
	if (!IsInputKeyDown(EKeys::LeftAlt) && !IsInputKeyDown(EKeys::RightAlt))
	{
		return;
	}

	if (_interfaceHidden)
	{
		showInterface();
		return;
	}

	// Every screen on the viewport, the action bar and the layers the menus open on alike, each with the visibility it had so it comes back the same.
	TArray<UUserWidget*> screens;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, screens, UUserWidget::StaticClass(), true);
	for (UUserWidget* screen : screens)
	{
		_hiddenScreens.Emplace(screen, screen->GetVisibility());
		screen->SetVisibility(ESlateVisibility::Collapsed);
	}

	_interfaceHidden = true;
}

void AClientPlayerController::showInterface()
{
	for (const TPair<TWeakObjectPtr<UUserWidget>, ESlateVisibility>& hidden : _hiddenScreens)
	{
		if (hidden.Key.IsValid())
		{
			hidden.Key->SetVisibility(hidden.Value);
		}
	}

	_hiddenScreens.Reset();
	_interfaceHidden = false;
}

void AClientPlayerController::toggleGameMenu()
{
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	UCommonActivatableWidgetStack* layer = (subsystem != nullptr) ? subsystem->getLayer(EUILayer::GameMenu) : nullptr;
	if ((layer == nullptr) || !subsystem->acceptEscape(false))
	{
		return;
	}

	// A second press closes what the first opened, rather than stacking another copy.
	// Options open over the menu take Escape themselves, so a press arriving here always meets the menu.
	// The death menu stays: Respawn is the only way out of it.
	UCommonActivatableWidget* open = layer->GetActiveWidget();
	if (Cast<UDeathMenuWidget>(open) != nullptr)
	{
		return;
	}

	if (open != nullptr)
	{
		open->DeactivateWidget();
		return;
	}

	// With nothing on screen a selection goes first: Escape lets go of the target, and only a press with nothing selected opens the menu.
	UServerConnectionSubsystem* connection = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if ((connection != nullptr) && connection->world().cancelTarget())
	{
		return;
	}

	openScreen(UGameMenuWidget::StaticClass());
}

void AClientPlayerController::showDeathMenu(TFunction<void()> onRespawn)
{
	if (_deathMenu.IsValid() && _deathMenu->IsActivated())
	{
		return;
	}

	UDeathMenuWidget* menu = Cast<UDeathMenuWidget>(openScreen(UDeathMenuWidget::StaticClass()));
	if (menu != nullptr)
	{
		menu->setup(MoveTemp(onRespawn));
		_deathMenu = menu;
	}
}

void AClientPlayerController::closeDeathMenu()
{
	if (_deathMenu.IsValid() && _deathMenu->IsActivated())
	{
		_deathMenu->DeactivateWidget();
	}

	_deathMenu.Reset();
}

void AClientPlayerController::showLoot(const Network::FCorpseLoot& loot, const FText& corpseName)
{
	const bool open = _loot.IsValid() && _loot->IsActivated();
	if (loot.stacks.IsEmpty())
	{
		// Nothing left, or the corpse gone: an open window on that corpse closes, and nothing opens for an empty one.
		if (open && (_loot->corpseId() == loot.corpseId))
		{
			_loot->DeactivateWidget();
		}

		return;
	}

	if (!open)
	{
		_loot = Cast<ULootWidget>(openScreen(ULootWidget::StaticClass()));

		// Looting lets go of the fallen creature, so its empty health bar goes; the server finds the corpse by its id, not by the selection.
		UGameInstance* gameInstance = GetGameInstance();
		UServerConnectionSubsystem* connection = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
		if (connection != nullptr)
		{
			connection->world().cancelTarget();
		}
	}

	if (_loot.IsValid())
	{
		_loot->show(loot, corpseName);
	}
}

void AClientPlayerController::toggleInventory()
{
	openOverBareWorld(UInventoryWidget::StaticClass());
}

void AClientPlayerController::toggleMap()
{
	openOverBareWorld(UMapWidget::StaticClass());
}

void AClientPlayerController::startChat()
{
	// A screen like the map, so CommonUI gives its box the keys; openScreen also brings back an interface Alt+H hid.
	openOverBareWorld(UChatLineWidget::StaticClass());
}

void AClientPlayerController::useShortcut(int32 index)
{
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* connection = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if ((connection != nullptr) && connection->world().isActive())
	{
		InventoryLayout::useShortcut(connection->world(), index);
	}
}

void AClientPlayerController::interact()
{
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	UCommonActivatableWidgetStack* layer = (subsystem != nullptr) ? subsystem->getLayer(EUILayer::GameMenu) : nullptr;
	UServerConnectionSubsystem* connection = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	UWorld* world = GetWorld();
	AWorldGameMode* mode = (world != nullptr) ? world->GetAuthGameMode<AWorldGameMode>() : nullptr;
	APawn* character = GetPawn();
	if ((connection == nullptr) || (mode == nullptr) || (character == nullptr) || ((layer != nullptr) && (layer->GetActiveWidget() != nullptr)))
	{
		return;
	}

	// A corpse underfoot is looted before a townsperson nearby is talked to.
	const uint32 corpseId = mode->corpseInFront(*character);
	if (corpseId != 0)
	{
		connection->world().openLoot(corpseId);
		return;
	}

	const uint32 objectId = mode->townspersonInFront(*character);
	if (objectId != 0)
	{
		connection->world().talkTo(objectId);
	}
}

void AClientPlayerController::attack()
{
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	UCommonActivatableWidgetStack* layer = (subsystem != nullptr) ? subsystem->getLayer(EUILayer::GameMenu) : nullptr;
	UServerConnectionSubsystem* connection = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	UWorld* world = GetWorld();
	AWorldGameMode* mode = (world != nullptr) ? world->GetAuthGameMode<AWorldGameMode>() : nullptr;
	AWorldCharacter* character = Cast<AWorldCharacter>(GetPawn());
	if ((connection == nullptr) || (mode == nullptr) || (character == nullptr) || ((layer != nullptr) && (layer->GetActiveWidget() != nullptr)))
	{
		return;
	}

	if (character->isFallen())
	{
		mode->offerReturnToVillage();
		return;
	}

	if (character->isAttacking())
	{
		return;
	}

	// The reach the server allows is the weapon's range and both bodies, and the swing is kept short of it so the server never has to walk the character in.
	bool armed = false;
	for (const Network::FInventoryItem& item : connection->world().items())
	{
		armed = armed || (item.equipped && (item.type2 == 0));
	}

	const double reachCm = (armed ? attackArmedReachUnits : attackUnarmedReachUnits) * Network::worldUnitCm;
	const FVector aim = FRotator(0.0f, GetControlRotation().Yaw, 0.0f).Vector();
	const uint32 objectId = mode->monsterInFront(character->GetActorLocation(), aim, reachCm);
	if (objectId != 0)
	{
		connection->world().attack(objectId);
		return;
	}

	character->strikeAir();
}

void AClientPlayerController::showNpcHtml(const Network::FNpcHtml& message)
{
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	UCommonActivatableWidgetStack* layer = (subsystem != nullptr) ? subsystem->getLayer(EUILayer::GameMenu) : nullptr;
	if (layer == nullptr)
	{
		return;
	}

	// The page a link led to replaces the one on screen. A page that arrives over any other screen has nowhere to go.
	UCommonActivatableWidget* open = layer->GetActiveWidget();
	UNpcDialogWidget* dialog = Cast<UNpcDialogWidget>(open);
	if ((dialog == nullptr) && (open == nullptr))
	{
		dialog = Cast<UNpcDialogWidget>(openScreen(UNpcDialogWidget::StaticClass()));

		// The cursor works the dialog, and the mouse would turn the camera while it moved.
		SetIgnoreLookInput(true);
	}

	if (dialog != nullptr)
	{
		dialog->showPage(message.html);

		// Only a page from a creature is tied to where that creature stands. Zero, and an object this client has not been told of, are the server's own pages.
		_dialogObjectId = message.npcObjectId;
	}
}

void AClientPlayerController::openOverBareWorld(TSubclassOf<UCommonActivatableWidget> screenClass)
{
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	UCommonActivatableWidgetStack* layer = (subsystem != nullptr) ? subsystem->getLayer(EUILayer::GameMenu) : nullptr;
	if ((layer == nullptr) || (layer->GetActiveWidget() != nullptr))
	{
		return;
	}

	openScreen(screenClass);
}

UCommonActivatableWidget* AClientPlayerController::openScreen(TSubclassOf<UCommonActivatableWidget> screenClass)
{
	// A screen opened over a hidden interface would take the keys while showing nothing.
	if (_interfaceHidden)
	{
		showInterface();
	}

	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	UCommonActivatableWidgetStack* layer = (subsystem != nullptr) ? subsystem->getLayer(EUILayer::GameMenu) : nullptr;
	if (layer == nullptr)
	{
		return nullptr;
	}

	// The layer is watched rather than the screen, because opening Options over the menu deactivates the menu without leaving it.
	if (!_watchingGameMenuLayer)
	{
		layer->OnDisplayedWidgetChanged().AddUObject(this, &AClientPlayerController::handleGameMenuLayerChanged);
		_watchingGameMenuLayer = true;
	}

	UCommonActivatableWidget* screen = subsystem->pushWidget(EUILayer::GameMenu, screenClass);
	if (screen == nullptr)
	{
		return nullptr;
	}

	// The screen is read with a pointer, and game and UI together still lets its keys through to close it again.
	SetShowMouseCursor(true);
	SetInputMode(FInputModeGameAndUI().SetWidgetToFocus(screen->TakeWidget()));
	return screen;
}

void AClientPlayerController::handleGameMenuLayerChanged(UCommonActivatableWidget* widget)
{
	if (widget == nullptr)
	{
		ResetIgnoreLookInput();
		SetShowMouseCursor(false);
		SetInputMode(FInputModeGameOnly());
	}
}
