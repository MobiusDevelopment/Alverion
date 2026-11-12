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
#include "UI/GameMenuWidget.h"
#include "UI/ChatLineWidget.h"
#include "UI/InventoryLayout.h"
#include "UI/InventoryWidget.h"
#include "UI/MapWidget.h"
#include "UI/NpcDialogWidget.h"
#include "InputAction.h"
#include "Core/InputBootstrapSubsystem.h"
#include "ServerConnectionSubsystem.h"
#include "UI/UISubsystem.h"
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
}

void AClientPlayerController::PlayerTick(float deltaSeconds)
{
	Super::PlayerTick(deltaSeconds);

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
	UCommonActivatableWidget* open = layer->GetActiveWidget();
	if (open != nullptr)
	{
		open->DeactivateWidget();
		return;
	}

	openScreen(UGameMenuWidget::StaticClass());
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

	const uint32 objectId = mode->townspersonInFront(*character);
	if (objectId != 0)
	{
		connection->world().talkTo(objectId);
	}
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
