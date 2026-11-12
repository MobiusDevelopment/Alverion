#include "Core/InputBootstrapSubsystem.h"

#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "PlayerMappableKeySettings.h"
#include "InputMappingContext.h"
#include "UserSettings/EnhancedInputUserSettings.h"

namespace
{
	struct FDefaultBinding
	{
		const TCHAR* actionPath;
		const TCHAR* primaryKeyName;
		const TCHAR* secondaryKeyName;
	};

	// The starter set. Keys are the defaults a player can rebind, and a saved rebind wins over what is here.
	// Every action carries two: a keyboard or mouse key, then a gamepad button, so an Xbox pad and the keyboard
	// both work at once rather than one replacing the other. Empty means that slot starts unbound.
	const FDefaultBinding defaultBindings[] =
	{
		{ TEXT("/Game/Input/Actions/IA_MoveForward.IA_MoveForward"), TEXT("W"), TEXT("Gamepad_LeftStick_Up") },
		{ TEXT("/Game/Input/Actions/IA_MoveBackward.IA_MoveBackward"), TEXT("S"), TEXT("Gamepad_LeftStick_Down") },
		{ TEXT("/Game/Input/Actions/IA_MoveLeft.IA_MoveLeft"), TEXT("A"), TEXT("Gamepad_LeftStick_Left") },
		{ TEXT("/Game/Input/Actions/IA_MoveRight.IA_MoveRight"), TEXT("D"), TEXT("Gamepad_LeftStick_Right") },
		{ TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"), TEXT("SpaceBar"), TEXT("Gamepad_FaceButton_Bottom") },
		{ TEXT("/Game/Input/Actions/IA_Interact.IA_Interact"), TEXT("E"), TEXT("Gamepad_FaceButton_Right") },
		{ TEXT("/Game/Input/Actions/IA_Attack.IA_Attack"), TEXT("LeftMouseButton"), TEXT("Gamepad_RightTrigger") },
		{ TEXT("/Game/Input/Actions/IA_Inventory.IA_Inventory"), TEXT("I"), TEXT("Gamepad_FaceButton_Top") },
		{ TEXT("/Game/Input/Actions/IA_Chat.IA_Chat"), TEXT("Enter"), TEXT("Gamepad_DPad_Up") },
		{ TEXT("/Game/Input/Actions/IA_Menu.IA_Menu"), TEXT("Escape"), TEXT("Gamepad_Special_Right") },
		{ TEXT("/Game/Input/Actions/IA_BackView.IA_BackView"), TEXT("V"), TEXT("Gamepad_RightThumbstick") },
		{ TEXT("/Game/Input/Actions/IA_Map.IA_Map"), TEXT("M"), TEXT("Gamepad_DPad_Down") },
		{ TEXT("/Game/Input/Actions/IA_SwitchCamera.IA_SwitchCamera"), TEXT("LeftShift"), TEXT("Gamepad_LeftShoulder") },
		{ TEXT("/Game/Input/Actions/IA_AutoRun.IA_AutoRun"), TEXT("NumLock"), TEXT("ThumbMouseButton") }
	};

	// The action bar's keys, the number row from 1 to 0, listed after the table on the options screen.
	const TCHAR* const inputShortcutKeyNames[] = { TEXT("One"), TEXT("Two"), TEXT("Three"), TEXT("Four"), TEXT("Five"), TEXT("Six"), TEXT("Seven"), TEXT("Eight"), TEXT("Nine"), TEXT("Zero") };

	// The rebindable name of a slot's action, which also keys its translated label as keybind.Shortcut1 and on.
	FName inputShortcutMappingName(int32 index)
	{
		return FName(*FString::Printf(TEXT("Shortcut%d"), index + 1));
	}
}

void UInputBootstrapSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
	Super::Initialize(collection);

	buildDefaultMappings();

	applyDefaultMappings();

	registerWithUserSettings();
}

void UInputBootstrapSubsystem::applyDefaultMappings()
{
	if (_defaultMappingContext == nullptr)
	{
		return;
	}

	ULocalPlayer* localPlayer = GetLocalPlayer<ULocalPlayer>();
	if (localPlayer == nullptr)
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* inputSubsystem = localPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (inputSubsystem == nullptr)
	{
		return;
	}

	// Adding one that is already applied is a no-op, so this needs no test for whether it has run before.
	inputSubsystem->AddMappingContext(_defaultMappingContext, 0);
}

void UInputBootstrapSubsystem::registerWithUserSettings()
{
	if (_defaultMappingContext == nullptr)
	{
		return;
	}

	ULocalPlayer* localPlayer = GetLocalPlayer<ULocalPlayer>();
	if (localPlayer == nullptr)
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* inputSubsystem = localPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (inputSubsystem == nullptr)
	{
		return;
	}

	// Registering is what puts the mappings in front of the options screen; without it the rebind list is empty.
	UEnhancedInputUserSettings* inputSettings = inputSubsystem->GetUserSettings();
	if (inputSettings == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("InputBootstrapSubsystem found no user settings, so rebinding has nothing to list."));
		return;
	}

	inputSettings->RegisterInputMappingContext(_defaultMappingContext);

	const UEnhancedPlayerMappableKeyProfile* profile = inputSettings->GetActiveKeyProfile();
	const int32 rowCount = (profile != nullptr) ? profile->GetPlayerMappingRows().Num() : -1;
	UE_LOG(LogTemp, Display, TEXT("InputBootstrapSubsystem registered %d mappings, key profile now holds %d rows."), _defaultMappingContext->GetMappings().Num(), rowCount);
}

// The mappable names in the order the table lists them, which is the order the options screen puts its rows in.
// Read off the actions rather than written down twice, since the name that keys a row lives on the asset and a second copy here would drift from it.
TArray<FName> UInputBootstrapSubsystem::getDefaultMappingOrder() const
{
	TArray<FName> names;

	for (const FDefaultBinding& binding : defaultBindings)
	{
		const UInputAction* action = LoadObject<UInputAction>(nullptr, binding.actionPath);
		if (action == nullptr)
		{
			continue;
		}

		const UPlayerMappableKeySettings* settings = action->GetPlayerMappableKeySettings();
		if ((settings == nullptr) || settings->Name.IsNone())
		{
			continue;
		}

		names.AddUnique(settings->Name);
	}

	for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(inputShortcutKeyNames)); index++)
	{
		names.AddUnique(inputShortcutMappingName(index));
	}

	return names;
}

UInputAction* UInputBootstrapSubsystem::getShortcutAction(int32 index) const
{
	return _shortcutActions.IsValidIndex(index) ? _shortcutActions[index].Get() : nullptr;
}

UInputMappingContext* UInputBootstrapSubsystem::getDefaultMappingContext() const
{
	return _defaultMappingContext;
}

void UInputBootstrapSubsystem::buildDefaultMappings()
{
	_defaultMappingContext = NewObject<UInputMappingContext>(this, TEXT("DefaultMappingContext"));

	for (const FDefaultBinding& binding : defaultBindings)
	{
		UInputAction* action = LoadObject<UInputAction>(nullptr, binding.actionPath);
		if (action == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("InputBootstrapSubsystem could not load %s, so it stays unbound."), binding.actionPath);
			continue;
		}

		// Two mappings for one action is what gives the options screen its primary and secondary slots.
		_defaultMappingContext->MapKey(action, FKey(binding.primaryKeyName));

		if (binding.secondaryKeyName != nullptr)
		{
			_defaultMappingContext->MapKey(action, FKey(binding.secondaryKeyName));
		}
	}

	// The action bar's keys, made here with the rebindable names an asset would otherwise carry.
	for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(inputShortcutKeyNames)); index++)
	{
		const FName name = inputShortcutMappingName(index);
		UMappableInputAction* action = NewObject<UMappableInputAction>(this, name);
		action->makeMappable(name, FText::Format(INVTEXT("Action Bar {0}"), FText::AsNumber(index + 1)));
		_shortcutActions.Add(action);
		_defaultMappingContext->MapKey(action, FKey(inputShortcutKeyNames[index]));
	}
}

void UMappableInputAction::makeMappable(FName name, const FText& displayName)
{
	PlayerMappableKeySettings = NewObject<UPlayerMappableKeySettings>(this);
	PlayerMappableKeySettings->Name = name;
	PlayerMappableKeySettings->DisplayName = displayName;
}
