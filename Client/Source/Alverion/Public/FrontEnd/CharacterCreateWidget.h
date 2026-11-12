#pragma once

#include "CommonActivatableWidget.h"
#include "Appearance.h"
#include "Appearance/AppearanceChoices.h"
#include "CoreMinimal.h"

#include "CharacterCreateWidget.generated.h"

class UButton;
class UCharacterSelectWidget;
class UUISubsystem;
class UEditableTextBox;
class UVerticalBox;
class UTextBlock;

// Names a new character and builds its face, then asks the server for it.
// Every field of AppearanceData with a control on screen travels packed, the rest keeping defaults.
// The server therefore stores a real choice from the first character anyone makes.
// The middle of the screen is left empty on purpose, since that is where the character stands.
UCLASS()
class ALVERION_API UCharacterCreateWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;

	// The camera lives on the mouse rather than on buttons: held left turns, the wheel zooms.
	virtual FReply NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;

protected:
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FGroomSelectionTest;
	friend class FElfPlayerCreationTest;
	friend class FOrcPlayerCreationTest;
#endif

	UFUNCTION()
	void handleCreateClicked();

	UFUNCTION()
	void handleBackClicked();

	// Holds the name to the length the game server accepts, since the box has no cap of its own.
	UFUNCTION()
	void handleNameChanged(const FText& text);

	UFUNCTION()
	void handleNameCommitted(const FText& text, ETextCommit::Type commitMethod);

	// One per choice, because OnClicked carries no payload and a button cannot say which it is.
	UFUNCTION()
	void handleSpeciesNextClicked();

	UFUNCTION()
	void handleSpeciesPrevClicked();

	UFUNCTION()
	void handleFemaleClicked();

	UFUNCTION()
	void handleMaleClicked();

	// One per arrow, since OnClicked carries no payload and a button cannot say which it is.
	UFUNCTION()
	void handleEyesPrevClicked();

	UFUNCTION()
	void handleEyesNextClicked();

	UFUNCTION()
	void handleHairPrevClicked();

	UFUNCTION()
	void handleHairNextClicked();

	UFUNCTION()
	void handleHairColorPrevClicked();

	uint8 chosenHairStyle() const;
	void cycleHair(int32 step);
	uint8 chosenEyebrows() const;
	void cycleEyebrows(int32 step);

	UFUNCTION()
	void handleHairColorNextClicked();

	UFUNCTION()
	void handleBeardPrevClicked();

	UFUNCTION()
	void handleBeardNextClicked();

	UFUNCTION()
	void handleMustachePrevClicked();

	UFUNCTION()
	void handleMustacheNextClicked();

	UFUNCTION()
	void handleHeadPrevClicked();

	UFUNCTION()
	void handleHeadNextClicked();

	UFUNCTION()
	void handleSkinPrevClicked();

	UFUNCTION()
	void handleSkinNextClicked();

	UFUNCTION()
	void handleEyebrowsPrevClicked();

	UFUNCTION()
	void handleEyebrowsNextClicked();

	void submit();
	void setMessage(const FText& text);

	// The current choices as the bytes the wire carries.
	// Shared by the preview and the create request, so the two cannot describe different people.
	Network::FAppearance chosenAppearance() const;
	TArray<uint8> packAppearance() const;

	// Asks the front end to stand the currently chosen body on its spot.
	// Called on activation and on every click of the toggle, since that click is the whole point.
	void showAvatar();

	// One button of a column, kept in that column's array so the lighting can move between them.
	// The caller binds the click, since only the caller knows which choice this one stands for.
	UButton* addChoice(UVerticalBox& column, const TCHAR* key, TArray<TObjectPtr<UButton>>& into);

	// An arrow, a label and an arrow, for a choice with more values than a pair.
	// Returns the label, since the caller is what knows how to word it and when it changed.
	// The two arrows come back for the caller to bind, the way addChoice hands back its button.
	// They cannot be bound in here at all.
	// AddUniqueDynamic is a macro that stringifies the name it is handed.
	// Passing a function pointer therefore stringifies the parameter rather than the handler.
	// The caption is a string table key rather than a word, since every row on this screen is translated.
	UTextBlock* addCycler(UVerticalBox& column, const FText& captionText, UButton*& outPrevious, UButton*& outNext);

	// The next or previous value, wrapping at both ends.
	static uint8 cycle(uint8 value, int32 step, uint8 count);

	// Runs the given work against the UI subsystem when there is one to run it against.
	// Six callers reached for it the same way, and each was three lines of the same two checks.
	void askFrontEnd(TFunctionRef<void(UUISubsystem&)> ask) const;

	// Repaints the choices from the current pick. Cheap enough to run on every click.
	void refreshChoices();

	UPROPERTY()
	TObjectPtr<UEditableTextBox> _nameBox;

	UPROPERTY()
	TObjectPtr<UButton> _createButton;

	UPROPERTY()
	TObjectPtr<UButton> _backButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> _messageText;

	// Female then male, so a button's place in its row is the value _sex holds.
	UPROPERTY()
	TArray<TObjectPtr<UButton>> _sexButtons;

	// The choice itself, one index per row, packed into the appearance block when it is sent or previewed.

	// Male, which is what the screen opens on, on the operator's instruction of 2026-08-13.
	uint8 _sex = 1;
	static constexpr uint8 speciesCount = static_cast<uint8>(Network::EAppearanceSpecies::Count);
	uint8 _speciesIndex = 0;
	uint8 _skinTones[speciesCount] = {};
	uint8 _eyeColors[speciesCount] = { 0, AppearanceChoices::donorEyeColor, AppearanceChoices::donorEyeColor };
	TOptional<uint8> _speciesHairStyles[speciesCount][2];
	uint8 _hairStyle = 0;

	// Zero, which is the brown the groom material was already built as.
	uint8 _hairColors[speciesCount] = {};

	// Zero, which is a clean chin.
	// Kept rather than cleared on a switch to female, since it is packed as zero for her anyway.
	// A man who switches away and back therefore finds the beard he had.
	uint8 _facialHair = 0;

	// Zero, which is a bare lip. Kept across a switch to female for the reason the beard is.
	uint8 _mustache = 0;
	uint8 _eyebrows = 0;
	TOptional<uint8> _speciesEyebrows[speciesCount][2];

	// The name the last request carried, handed to the select screen once the server accepts it.
	FString _submittedName;

	// Which face of the roster, counted from zero, and filed by sex on the applier's side.
	// Zero is the first of whichever list the sex picks rather than a face nobody chose.
	uint8 _head = 0;

	// The two cycler labels, repainted by refreshChoices when their value moves.
	UPROPERTY()
	TObjectPtr<UTextBlock> _eyeColorLabel;

	UPROPERTY()
	TObjectPtr<UTextBlock> _hairStyleLabel;

	UPROPERTY()
	TObjectPtr<UTextBlock> _hairColorLabel;

	UPROPERTY()
	TObjectPtr<UTextBlock> _facialHairLabel;

	UPROPERTY()
	TObjectPtr<UTextBlock> _mustacheLabel;

	UPROPERTY()
	TObjectPtr<UTextBlock> _eyebrowsLabel;

	UPROPERTY()
	TObjectPtr<UTextBlock> _speciesLabel;

	UPROPERTY()
	TObjectPtr<UTextBlock> _headLabel;

	// True between pressing the left button on the empty screen and letting go of it.
	// Held here rather than asked of Slate, since a drag that leaves the window still has to end.
	bool _dragging = false;

	UPROPERTY()
	TObjectPtr<UTextBlock> _skinToneLabel;
};
