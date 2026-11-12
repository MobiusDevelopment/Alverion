#pragma once

#include "CommonActivatableWidget.h"
#include "CoreMinimal.h"

#include "CharacterSelectWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;

// The account's character, standing on its spot with its name under its feet.
// Shown once the game server sends the character list.
//
// One character at a time rather than a column of rows.
// The screen is the character.
// A list in front of it was the UI competing with what the camera moved here to show.
// Arrows either side of the name reach the rest, and they only appear when there are others to reach.
UCLASS()
class ALVERION_API UCharacterSelectWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnActivated() override;

	// Abandons the session when the screen went away without a choice.
	// Destruction rather than deactivation, because a screen pushed over this one deactivates it too,
	// and being covered is not the same as being left.
	virtual void NativeDestruct() override;

	// Called by a row. Sends the selection and closes the screen.
	void chooseCharacter(uint32 objectId);

	// Called by a row. Asks the player first, since a deletion cannot be taken back from here.
	void confirmDelete(uint32 objectId);

	// Refills the screen from the list the game server last sent, keeping the character shown where it can.
	void refresh();

protected:
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

	// Enter on the keyboard is the Enter button, wherever the focus happens to be sitting.
	virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

private:
	// Steps the shown character by one in either direction, wrapping at both ends.
	void cycleShown(int32 step);

	UFUNCTION()
	void handleEnterClicked();

	UFUNCTION()
	void handleDeleteClicked();

	UFUNCTION()
	void handleCreateClicked();

	// The character before and after the one standing on its spot, wrapping at both ends.
	UFUNCTION()
	void handlePreviousClicked();

	UFUNCTION()
	void handleNextClicked();

	UFUNCTION()
	void handleServerListClicked();

	UFUNCTION()
	void handleExitClicked();

	// Hidden while a character is on its spot, since they caption themselves.
	// Shown as the creation prompt when the account has nobody.
	UPROPERTY()
	TObjectPtr<UTextBlock> _title;

	// Under the character's feet, so the name reads as belonging to who is standing there.
	UPROPERTY()
	TObjectPtr<UTextBlock> _nameLabel;

	// Under the name, only while the server is waiting to delete the character.
	UPROPERTY()
	TObjectPtr<UTextBlock> _deletionLabel;

	// Beneath the name, and only when there is a character for it to enter the world with.
	UPROPERTY()
	TObjectPtr<UVerticalBox> _nameColumn;

	UPROPERTY()
	TObjectPtr<UButton> _enterButton;

	// Bottom right, stacked. Delete goes when the account has nothing to delete.
	UPROPERTY()
	TObjectPtr<UButton> _deleteButton;

	UPROPERTY()
	TObjectPtr<UButton> _createButton;

	UPROPERTY()
	TObjectPtr<UButton> _serverListButton;

	UPROPERTY()
	TObjectPtr<UButton> _exitButton;

	// Either side of the name, and collapsed unless the account has somebody to switch to.
	UPROPERTY()
	TObjectPtr<UButton> _previousButton;

	UPROPERTY()
	TObjectPtr<UButton> _nextButton;

	// Which of the account's characters is standing there, counted from zero.
	// Held rather than derived, since the list can be refilled while this screen is up.
	// Clamped by refresh rather than trusted, because a deletion shortens the list underneath it.
	int32 _shownIndex = 0;

	// Who Enter and Delete act on. Zero while the account has no characters.
	uint32 _shownObjectId = 0;

	// True once a character was chosen, so leaving without choosing can be told apart.
	bool _chose = false;
};
