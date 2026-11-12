#pragma once

#include "UI/ConfirmDialog.h"
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "UISubsystem.generated.h"

class UCommonActivatableWidget;
class UCommonActivatableWidgetStack;

// The four layers every screen is pushed onto. Game sits underneath everything, Modal on top.
UENUM(BlueprintType)
enum class EUILayer : uint8
{
	Game,
	GameMenu,
	Menu,
	Modal
};

// The two places a front end avatar can stand: tagged marker actors in L_Login, so moving one is a drag in the editor.
// An enum rather than the tag itself, so a screen cannot ask for a spot by misspelling it.
UENUM(BlueprintType)
enum class EAvatarSpot : uint8
{
	Select,
	Create
};

// Owns the widget stacks the front end and the in-world HUD share. It outlives level travel, which is
// what lets the same options widget class be pushed from the main menu and from the in world game menu. Nothing
// pauses when that menu opens: the server owns the simulation and keeps running regardless of what the client
// happens to be showing.
UCLASS()
class ALVERION_API UUISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Called by the root layout once its stacks exist. Registering a layer twice replaces the stack, which is
	// what happens when a new root layout is created after level travel.
	UFUNCTION(BlueprintCallable, Category = "UI")
	void registerLayer(EUILayer layer, UCommonActivatableWidgetStack* stack);

	UFUNCTION(BlueprintCallable, Category = "UI")
	void unregisterLayer(EUILayer layer);

	// Returns the pushed widget, or nullptr when the layer has no stack or the class is unset.
	UFUNCTION(BlueprintCallable, Category = "UI")
	UCommonActivatableWidget* pushWidget(EUILayer layer, TSubclassOf<UCommonActivatableWidget> widgetClass);

	UFUNCTION(BlueprintCallable, Category = "UI")
	void popWidget(EUILayer layer);

	UFUNCTION(BlueprintPure, Category = "UI")
	UCommonActivatableWidgetStack* getLayer(EUILayer layer) const;

	// Supplied by the root layout alongside its stacks, so no asset path is hardcoded here.
	UFUNCTION(BlueprintCallable, Category = "UI")
	void registerConfirmDialogClass(TSubclassOf<UConfirmDialog> dialogClass);

	// Asks a yes or no question on the Modal layer.
	// The delegate runs exactly once, and it runs with false when the dialog cannot be shown,
	// so a caller never treats an unanswerable question as a yes.
	void askConfirm(const FText& message, FOnConfirmAnswered onAnswered);

	// Installed by the front end game mode, the only thing that can see the level, and unset on any level without one, so every caller checks first.
	// The appearance travels as its packed bytes, so a screen never learns what a byte means. Empty bytes are legal and mean the defaults.
	// The game mode captures itself weakly, so one left behind by a dead game mode does nothing.
	// The items worn travel the same way, as the server's item ids; none at all stands the body in its underwear.
	TFunction<void(EAvatarSpot spot, TConstArrayView<uint8> appearance, TConstArrayView<int32> worn)> showAvatar;
	TFunction<void()> clearAvatar;

	// Turns whoever is standing, in degrees, so a face can be seen from more than one side.
	// The body turns rather than the camera, since the camera stops are placed by hand.
	// Moving one to look at an ear would undo a framing decision somebody made by eye.
	TFunction<void(float degrees)> turnAvatar;

	// Narrows or widens the field of view of whichever stop is live, in degrees.
	// The camera itself never moves, and the screen restores what it found on the way out.
	TFunction<void(float degrees)> zoomView;

	// Set by the character screen's Server List button. The login screen sees it when it is uncovered and logs in again to show the list,
	// since the login server link closes once a server is chosen.
	bool forceServerList = false;

	// The name of a character just created, so the character screen lands on it once the refreshed list arrives.
	// The server answers a creation before it sends the list, so the new character's id is not known at that moment.
	FString createdName;

	// Whether an Escape press may close or open a screen: never a held key's repeat, and only after Escape has rested 300 milliseconds.
	// A burst of presses then closes one screen rather than running through all of them and opening the menu again.
	bool acceptEscape(bool isRepeat);

private:
	double _lastEscapeSeconds = 0.0;

	UPROPERTY()
	TMap<EUILayer, TObjectPtr<UCommonActivatableWidgetStack>> _layers;

	UPROPERTY()
	TSubclassOf<UConfirmDialog> _confirmDialogClass;
};
