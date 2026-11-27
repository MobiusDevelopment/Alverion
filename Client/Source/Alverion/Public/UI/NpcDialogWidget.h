#pragma once

#include "CommonActivatableWidget.h"
#include "CoreMinimal.h"
#include "UI/NpcHtml.h"

#include "NpcDialogWidget.generated.h"

class UButton;
class UWidget;
class UNpcDialogWidget;
class USoundBase;
class UScrollBox;
class UTextBlock;

/** What one link or button in the dialog does when it is clicked. A click carries no word of which button it was, so each link gets one of these to say. */
UCLASS()
class ALVERION_API UNpcDialogLink : public UObject
{
	GENERATED_BODY()

public:
	void setup(UNpcDialogWidget* dialog, NpcHtml::ELinkKind kind, const FString& target);

	UFUNCTION()
	void handleClicked();

private:
	UPROPERTY()
	TObjectPtr<UNpcDialogWidget> _dialog;

	NpcHtml::ELinkKind _kind = NpcHtml::ELinkKind::None;
	FString _target;
};

/**
 * A townsperson's dialog, pushed onto the game menu layer over the running world.
 * It shows the page the server sent, laid out by the HTML reader, and its links and buttons send the server's commands.
 * The server answers a click with another page, which replaces this one. Escape or the close button closes it.
 */
UCLASS()
class ALVERION_API UNpcDialogWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Shows a page in place of the one on screen, from the top. Safe before the widget has been built, in which case it shows when it is. */
	void showPage(const FString& html);

	/** Sends what a link does, with the fields of the page filled into its command, and leaves the dialog as it is until the server sends the page it leads to. */
	void follow(NpcHtml::ELinkKind kind, const FString& target);

protected:
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

	/** Closing the dialog, however it closes, lets go of the townsperson, so their health bar goes with it. */
	virtual void NativeOnDeactivated() override;

	/** Menu and game input together, so the cursor works the dialog and the movement keys still walk the character, as with the stock client. */
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	/** While the keyboard is in an entry box the keys go to the box alone, so typing a W does not walk the character. */
	virtual void NativeOnFocusChanging(const FWeakWidgetPath& previousFocusPath, const FWidgetPath& newWidgetPath, const FFocusEvent& focusEvent) override;

	/** Escape closes the dialog here: CommonUI holds the game's keys back while a screen is up, so the menu key never reaches the controller. */
	virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

	UPROPERTY(EditDefaultsOnly, Category = "Npc Dialog")
	TObjectPtr<USoundBase> clickSound;

private:
	void playClickSound() const;

	/** Lays the page out again from the parsed lines. */
	void showLines();

	/** One line of the page: a gap, a wrapped text block, a single link as a button, or a row of words and buttons that wraps. */
	void addLine(const NpcHtml::FLine& line);

	UButton* makeLinkButton(const NpcHtml::FRun& run);

	/** An entry box, a multi-line box, a drop-down list or the blank of an image, sized as the page asked. The first three are remembered by name. */
	UWidget* makeElement(const NpcHtml::FRun& run);

	/** What each named field of the page holds now. */
	TMap<FString, FString> fieldValues() const;

	UFUNCTION()
	void handleCloseClicked();

	UPROPERTY()
	TObjectPtr<UTextBlock> _title;

	UPROPERTY()
	TObjectPtr<UScrollBox> _content;

	UPROPERTY()
	TObjectPtr<UButton> _closeButton;

	/** The entry boxes and lists of the page on screen by the name the page gave them. */
	UPROPERTY()
	TMap<FString, TObjectPtr<UWidget>> _fields;

	/** The links of the page on screen, kept here because a button only holds a weak reference to what it calls. */
	UPROPERTY()
	TArray<TObjectPtr<UNpcDialogLink>> _links;

	NpcHtml::FDocument _document;
	bool _hasPage = false;

	/** Whether the keyboard is in an entry box. */
	bool _typing = false;
};
