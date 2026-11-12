#include "FrontEnd/CharacterSelectWidget.h"

#include "Blueprint/WidgetTree.h"
#include "FrontEnd/CharacterCreateWidget.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Core/GameSettings.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/LocalizedText.h"
#include "ServerConnectionSubsystem.h"
#include "UI/UISubsystem.h"
#include "UI/UIStyle.h"

// Named rather than anonymous, because the module builds unity.
// A helper name like the one below is exactly what a second screen picks too.
// That is how the last three of these collisions were found.
namespace characterSelect
{
	// How far the name sits off the bottom edge.
	// Below the feet rather than across the shins, which is where 260 and then 180 put it.
	// A number from looking at the shot, and the third time it was tuned.
	//
	// The shared control padding now, rather than a number of its own that sat ten pixels above it.
	// Enter and Exit Game are the two ends of one row and belong on one line.
	// Sharing the constant is also what stops them drifting apart the next time either moves.
	constexpr float nameBottomPad = UIStyle::frontEndControlsBottomPad;

	// One width for the corner stack and for Enter, so every button on the screen matches.
	// Enter is not in that stack and still measures the same, which is the point of sharing this.
	constexpr float actionColumnWidth = 200.0f;
	constexpr float actionSidePad = 90.0f;

	// The switching arrows either side of the name, at the width the creation screen's rows use.
	// One glyph on a button, so the width is the button rather than the text.
	constexpr float arrowWidth = 44.0f;

	// What the name is given between them, so the arrows hold still as names change length.
	constexpr float nameWidth = 320.0f;

	// Every button in that stack after the first, spaced from the one above and filling the width.
	void addAction(UVerticalBox& column, UButton* button)
	{
		UVerticalBoxSlot* slot = column.AddChildToVerticalBox(button);
		slot->SetHorizontalAlignment(HAlign_Fill);
		slot->SetPadding(FMargin(0.0f, UIStyle::rowSpacing, 0.0f, 0.0f));
	}

	// How long the server waits before deleting: whole days while a day or more is left, then whole hours, each rounded up so the last hour reads as one rather than none.
	FText deletionText(int32 seconds)
	{
		constexpr int32 hour = 60 * 60;
		constexpr int32 day = 24 * hour;
		if (seconds >= day)
		{
			return FText::Format(LocalizedText::get(TEXT("charselect.deleting_days")), (seconds + day - 1) / day);
		}

		return FText::Format(LocalizedText::get(TEXT("charselect.deleting_hours")), FMath::Max(1, (seconds + hour - 1) / hour));
	}
}

TSharedRef<SWidget> UCharacterSelectWidget::RebuildWidget()
{
	// Built here rather than in NativeConstruct.
	// This is the one place a native widget is guaranteed to have a tree to build into.
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		// The title owns the top, the list keeps to the left, the character on its spot keeps the rest.
		// A scrim would have covered the very thing the camera moved here to show.
		UOverlay* root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		UImage* wash = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush washBrush;
		washBrush.TintColor = FSlateColor(UIStyle::frontEndWashColor);
		wash->SetBrush(washBrush);

		UOverlaySlot* washSlot = root->AddChildToOverlay(wash);
		washSlot->SetHorizontalAlignment(HAlign_Fill);
		washSlot->SetVerticalAlignment(VAlign_Fill);

		_title = UIStyle::makeLabel(*WidgetTree, LocalizedText::get(TEXT("charselect.title")), UIStyle::frontEndTitleFontSize, UIStyle::frontEndTitleColor);
		FSlateFontInfo titleFont = UIStyle::font(UIStyle::frontEndTitleFontSize);
		titleFont.LetterSpacing = UIStyle::frontEndTitleLetterSpacing;
		_title->SetFont(titleFont);
		_title->SetJustification(ETextJustify::Center);

		UOverlaySlot* titleSlot = root->AddChildToOverlay(_title);
		titleSlot->SetHorizontalAlignment(HAlign_Center);
		titleSlot->SetVerticalAlignment(VAlign_Top);
		titleSlot->SetPadding(FMargin(0.0f, UIStyle::frontEndTitleTopPad, 0.0f, 0.0f));

		// The name and the way in, centred under the character rather than beside them.
		// Measured off the bottom edge, since the feet sit at a fixed place in the shot.
		// The camera that framed them is not going to move on its own.
		_nameColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		_nameLabel = UIStyle::makeLabel(*WidgetTree, FText::GetEmpty(), UIStyle::bodyFontSize, UIStyle::labelColor);
		_nameLabel->SetJustification(ETextJustify::Center);

		// The name between two arrows, which is the whole of switching character.
		// Populate collapses both while the account has only one, so a lone character keeps its old screen.
		UHorizontalBox* nameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		_previousButton = UIStyle::frontEndButton(*WidgetTree, FText::FromString(TEXT("<")));
		_previousButton->OnClicked.AddUniqueDynamic(this, &UCharacterSelectWidget::handlePreviousClicked);

		USizeBox* previousBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		previousBox->SetWidthOverride(characterSelect::arrowWidth);
		previousBox->AddChild(_previousButton);
		nameRow->AddChildToHorizontalBox(previousBox)->SetVerticalAlignment(VAlign_Center);

		// Fixed rather than sized to the name, so the arrows do not walk about as characters change.
		USizeBox* nameBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		nameBox->SetWidthOverride(characterSelect::nameWidth);
		nameBox->AddChild(_nameLabel);
		nameRow->AddChildToHorizontalBox(nameBox)->SetVerticalAlignment(VAlign_Center);

		_nextButton = UIStyle::frontEndButton(*WidgetTree, FText::FromString(TEXT(">")));
		_nextButton->OnClicked.AddUniqueDynamic(this, &UCharacterSelectWidget::handleNextClicked);

		USizeBox* nextBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		nextBox->SetWidthOverride(characterSelect::arrowWidth);
		nextBox->AddChild(_nextButton);
		nameRow->AddChildToHorizontalBox(nextBox)->SetVerticalAlignment(VAlign_Center);

		_nameColumn->AddChildToVerticalBox(nameRow)->SetHorizontalAlignment(HAlign_Center);

		_deletionLabel = UIStyle::makeLabel(*WidgetTree, FText::GetEmpty(), UIStyle::bodyFontSize, UIStyle::mutedColor);
		_deletionLabel->SetJustification(ETextJustify::Center);
		_deletionLabel->SetVisibility(ESlateVisibility::Collapsed);
		_nameColumn->AddChildToVerticalBox(_deletionLabel)->SetHorizontalAlignment(HAlign_Center);

		_enterButton = UIStyle::frontEndButton(*WidgetTree, LocalizedText::get(TEXT("menu.enter")));
		_enterButton->OnClicked.AddUniqueDynamic(this, &UCharacterSelectWidget::handleEnterClicked);

		// Sized to the corner stack rather than to its own label.
		// Every button here then measures the same whether or not it sits in that column.
		USizeBox* enterBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		enterBox->SetWidthOverride(characterSelect::actionColumnWidth);
		enterBox->AddChild(_enterButton);

		UVerticalBoxSlot* enterSlot = _nameColumn->AddChildToVerticalBox(enterBox);
		enterSlot->SetHorizontalAlignment(HAlign_Center);
		enterSlot->SetPadding(FMargin(0.0f, UIStyle::rowSpacing, 0.0f, 0.0f));

		UOverlaySlot* nameSlot = root->AddChildToOverlay(_nameColumn);
		nameSlot->SetHorizontalAlignment(HAlign_Center);
		nameSlot->SetVerticalAlignment(VAlign_Bottom);
		nameSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, characterSelect::nameBottomPad));

		// Everything that is not about this particular character, stacked in the corner.
		UVerticalBox* actions = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		_deleteButton = UIStyle::frontEndButton(*WidgetTree, LocalizedText::get(TEXT("menu.delete")));
		_deleteButton->OnClicked.AddUniqueDynamic(this, &UCharacterSelectWidget::handleDeleteClicked);
		actions->AddChildToVerticalBox(_deleteButton)->SetHorizontalAlignment(HAlign_Fill);

		_createButton = UIStyle::frontEndButton(*WidgetTree, LocalizedText::get(TEXT("menu.create")));
		_createButton->OnClicked.AddUniqueDynamic(this, &UCharacterSelectWidget::handleCreateClicked);
		characterSelect::addAction(*actions, _createButton);

		_serverListButton = UIStyle::frontEndButton(*WidgetTree, LocalizedText::get(TEXT("charselect.server_list")));
		_serverListButton->OnClicked.AddUniqueDynamic(this, &UCharacterSelectWidget::handleServerListClicked);
		characterSelect::addAction(*actions, _serverListButton);

		_exitButton = UIStyle::frontEndButton(*WidgetTree, LocalizedText::get(TEXT("charselect.exit_game")));
		_exitButton->OnClicked.AddUniqueDynamic(this, &UCharacterSelectWidget::handleExitClicked);
		characterSelect::addAction(*actions, _exitButton);

		USizeBox* actionBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		actionBox->SetWidthOverride(characterSelect::actionColumnWidth);
		actionBox->AddChild(actions);

		UOverlaySlot* actionSlot = root->AddChildToOverlay(actionBox);
		actionSlot->SetHorizontalAlignment(HAlign_Right);
		actionSlot->SetVerticalAlignment(VAlign_Bottom);
		actionSlot->SetPadding(FMargin(0.0f, 0.0f, characterSelect::actionSidePad, UIStyle::frontEndControlsBottomPad));

		WidgetTree->RootWidget = root;
	}

	return Super::RebuildWidget();
}

void UCharacterSelectWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// Filled on activation rather than on construction.
	// The list arrives from the game server after this class is known and before it is shown.
	// Cleared first, so a screen shown again opens on the remembered character as a fresh one does.
	_shownObjectId = 0;
	refresh();

	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (network == nullptr)
	{
		return;
	}

	// The list itself comes back through the login screen, which owns the flow's steps and calls refresh. Only a refusal is told here.
	network->gameServer().onDeleteRejected.BindWeakLambda(this, [this]()
	{
		UE_LOG(LogTemp, Warning, TEXT("The server refused the deletion."));
	});
}

void UCharacterSelectWidget::refresh()
{
	if ((_nameLabel == nullptr) || (WidgetTree == nullptr))
	{
		return;
	}

	// Nobody shown yet means the screen has just been arrived on, since activation clears it.
	bool showRemembered = (_shownObjectId == 0);
	_shownObjectId = 0;

	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (network == nullptr)
	{
		return;
	}

	const TArray<Network::FCharacterListEntry>& characters = network->gameServer().characters();

	// The one played or made last, which is what a player expects to be looking at on arrival.
	// It survives a restart because it is kept in the settings beside the remembered server.
	// Shown on arrival and after a creation, so a character created while this screen was alive is the one it comes back to; paging with the arrows keeps whoever was paged to.
	// A character just created is remembered by name, since the server answers the creation before it sends the list holding its id.
	UGameSettings* settings = UGameSettings::get();
	UUISubsystem* createdUi = gameInstance->GetSubsystem<UUISubsystem>();
	if ((settings != nullptr) && (createdUi != nullptr) && !createdUi->createdName.IsEmpty())
	{
		const FString createdName = createdUi->createdName;
		const Network::FCharacterListEntry* created = characters.FindByPredicate([&createdName](const Network::FCharacterListEntry& candidate)
		{
			return candidate.name == createdName;
		});

		if (created != nullptr)
		{
			settings->rememberCharacter(created->objectId);
			createdUi->createdName.Reset();
			showRemembered = true;
		}
	}

	const uint64 remembered = (settings != nullptr) ? settings->getRememberedCharacter() : 0;

	if (showRemembered && (remembered != 0))
	{
		const int32 rememberedIndex = characters.IndexOfByPredicate([remembered](const Network::FCharacterListEntry& candidate)
		{
			return candidate.objectId == remembered;
		});

		if (rememberedIndex != INDEX_NONE)
		{
			_shownIndex = rememberedIndex;
		}
	}

	// Clamped rather than trusted, since the list is refilled under this screen.
	// A deletion shortens it, and the index left behind would otherwise read past the end.
	if (!characters.IsValidIndex(_shownIndex))
	{
		_shownIndex = (characters.Num() > 0) ? (characters.Num() - 1) : 0;
	}

	const Network::FCharacterListEntry* shown = characters.IsValidIndex(_shownIndex) ? &characters[_shownIndex] : nullptr;

	if (shown != nullptr)
	{
		_shownObjectId = shown->objectId;
		_nameLabel->SetText(FText::FromString(shown->name));
	}
	else
	{
		_nameLabel->SetText(FText::GetEmpty());
	}

	// A character the server is waiting to delete says how long it has left, and Delete turns into Restore for it.
	const bool deleting = (shown != nullptr) && (shown->deleteSeconds > 0);
	if (_deletionLabel != nullptr)
	{
		_deletionLabel->SetText(deleting ? characterSelect::deletionText(shown->deleteSeconds) : FText::GetEmpty());
		_deletionLabel->SetVisibility(deleting ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	UTextBlock* deleteLabel = (_deleteButton != nullptr) ? Cast<UTextBlock>(_deleteButton->GetChildAt(0)) : nullptr;
	if (deleteLabel != nullptr)
	{
		deleteLabel->SetText(LocalizedText::get(deleting ? TEXT("menu.restore") : TEXT("menu.delete")));
	}

	// The body on its spot is spawned by the front end game mode, which is what can see the level.
	// The character's own stored appearance decides it rather than a default chosen here.
	// Somebody made female therefore comes back female, wearing the hair and eyes they were given.
	// An entry with no appearance bytes stands as the defaults rather than not standing.
	// That is the right answer for a character made before appearance reached the wire.
	UUISubsystem* ui = gameInstance->GetSubsystem<UUISubsystem>();
	if (ui != nullptr)
	{
		if (shown != nullptr)
		{
			// Passed on as stored rather than unpacked here.
			// The game mode is what applies them, so this screen never learns what a byte means.
			if (ui->showAvatar)
			{
				ui->showAvatar(EAvatarSpot::Select, shown->appearance, shown->worn);
			}
		}
		else if (ui->clearAvatar)
		{
			ui->clearAvatar();
		}
	}

	// The character is the screen and needs no caption, so the title only speaks when there is
	// nobody standing there. Then it stops naming what this screen is and says what to do about it.
	if (_title != nullptr)
	{
		_title->SetText(LocalizedText::get(TEXT("charcreate.title")));
		_title->SetVisibility((shown != nullptr) ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	// Entering and deleting both need somebody to act on.
	// Collapsed rather than disabled, since a button that can never do anything here is noise.
	const ESlateVisibility whenPresent = (shown != nullptr) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

	if (_enterButton != nullptr)
	{
		_enterButton->SetVisibility(whenPresent);
	}

	if (_deleteButton != nullptr)
	{
		_deleteButton->SetVisibility(whenPresent);
	}

	// Arrows only when there is somebody else to reach.
	// Collapsed rather than disabled, for the same reason Enter and Delete are.
	const ESlateVisibility whenSeveral = (characters.Num() > 1) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

	if (_previousButton != nullptr)
	{
		_previousButton->SetVisibility(whenSeveral);
	}

	if (_nextButton != nullptr)
	{
		_nextButton->SetVisibility(whenSeveral);
	}

	// Focus taken rather than asked for.
	//
	// RequestRefreshFocus was tried first and did nothing, measured on 2026-08-13.
	// Under PIE the action router never registered this screen at all.
	// Its log names LoginWidget as the leaf-most node before the push and says nothing after it.
	// The standalone client does register it, measured the same day, so the fault is PIE's alone.
	// Slate is asked directly regardless, which needs no router and behaves the same in both.
	//
	// Next tick rather than now, since a widget pushed this frame has not been laid out yet.
	// Focus needs a laid out widget, and the screenshot commands wait for the same reason.
	UWorld* world = GetWorld();
	if ((world == nullptr) || (_enterButton == nullptr) || (_shownObjectId == 0))
	{
		return;
	}

	world->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (_enterButton == nullptr)
		{
			return;
		}

		_enterButton->SetKeyboardFocus();

		UE_LOG(LogTemp, Display, TEXT("CharacterSelectWidget put the focus on the enter button, which now has it: %s."), _enterButton->HasKeyboardFocus() ? TEXT("yes") : TEXT("no"));
	}));
}

void UCharacterSelectWidget::chooseCharacter(uint32 objectId)
{
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (network == nullptr)
	{
		return;
	}

	// The flow refuses a character the list did not carry.
	// A refusal here means the list moved underneath the screen, and closing on it would hide that.
	if (!network->gameServer().selectCharacter(objectId))
	{
		return;
	}

	// Remembered here rather than on arrival in the world, because this is the choice the player made and the world may refuse it.
	UGameSettings* settings = UGameSettings::get();
	if (settings != nullptr)
	{
		settings->rememberCharacter(objectId);
	}

	_chose = true;
	DeactivateWidget();
}

void UCharacterSelectWidget::confirmDelete(uint32 objectId)
{
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	if (subsystem == nullptr)
	{
		return;
	}

	UServerConnectionSubsystem* network = gameInstance->GetSubsystem<UServerConnectionSubsystem>();
	if (network == nullptr)
	{
		return;
	}

	// The name is worth putting in the question, since that is what the player recognises.
	const Network::FCharacterListEntry* entry = network->gameServer().characters().FindByPredicate([objectId](const Network::FCharacterListEntry& candidate)
	{
		return candidate.objectId == objectId;
	});

	if (entry == nullptr)
	{
		return;
	}

	// Weak, because the answer arrives after the dialog closes and this screen may be gone by then.
	// menu.delete_confirm already carries the warning that this cannot be undone,
	// and it is already translated everywhere, so the question borrows it rather than adding a key.
	subsystem->askConfirm(FText::Format(LocalizedText::get(TEXT("menu.delete_confirm")), FText::FromString(entry->name)), FOnConfirmAnswered::CreateWeakLambda(this, [this, objectId](bool confirmed)
	{
		if (!confirmed)
		{
			return;
		}

		UGameInstance* instance = GetGameInstance();
		UServerConnectionSubsystem* network = (instance != nullptr) ? instance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
		if (network != nullptr)
		{
			network->gameServer().deleteCharacter(objectId);
		}
	}));
}

void UCharacterSelectWidget::handleCreateClicked()
{
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	if (subsystem == nullptr)
	{
		return;
	}

	// The same layer, so this screen is covered rather than left showing through the one on top.
	// Closing the creation screen reactivates this one, and activation is what refills the rows,
	// so a character made over there appears here without anything being told about it.
	subsystem->pushWidget(EUILayer::Menu, UCharacterCreateWidget::StaticClass());
}

// Enter enters the world, which is what the key says and what the button under the name does.
//
// Bubbled rather than bound to the button, so it works wherever the focus sits.
// A player who has just clicked Delete and cancelled it should not have to find the button again.
// The handler refuses on an empty account by itself, so nothing is checked twice here.
FReply UCharacterSelectWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
	// The arrow keys do what the arrows either side of the name do, wherever the focus is sitting.
	if (keyEvent.GetKey() == EKeys::Left)
	{
		cycleShown(-1);

		return FReply::Handled();
	}

	if (keyEvent.GetKey() == EKeys::Right)
	{
		cycleShown(1);

		return FReply::Handled();
	}

	if (keyEvent.GetKey() != EKeys::Enter)
	{
		return Super::NativeOnKeyDown(geometry, keyEvent);
	}

	handleEnterClicked();

	return FReply::Handled();
}

void UCharacterSelectWidget::handlePreviousClicked()
{
	cycleShown(-1);
}

void UCharacterSelectWidget::handleNextClicked()
{
	cycleShown(1);
}

void UCharacterSelectWidget::cycleShown(int32 step)
{
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (network == nullptr)
	{
		return;
	}

	const int32 count = network->gameServer().characters().Num();
	if (count < 2)
	{
		return;
	}

	// Wrapped both ways, since a row of characters has no ends worth stopping at.
	// The modulo is taken twice because the first can come back negative going left.
	_shownIndex = ((_shownIndex + step) % count + count) % count;

	// Everything on this screen follows the index, including the body on its spot.
	refresh();
}

void UCharacterSelectWidget::handleEnterClicked()
{
	// Zero means the account has none, and the button that calls this is collapsed in that case.
	if (_shownObjectId != 0)
	{
		chooseCharacter(_shownObjectId);
	}
}

void UCharacterSelectWidget::handleDeleteClicked()
{
	if (_shownObjectId == 0)
	{
		return;
	}

	// The flow restores only a character the server is waiting to delete, and taking a deletion back needs no second thought.
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if ((network != nullptr) && network->gameServer().restoreCharacter(_shownObjectId))
	{
		return;
	}

	confirmDelete(_shownObjectId);
}

void UCharacterSelectWidget::handleServerListClicked()
{
	// Asked for on purpose, so the login screen must not take the remembered server again.
	// Without this the screen closes onto a login that auto selects straight back here, which is
	// what pressing Server List looked like once a remembered server existed.
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* ui = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	if (ui != nullptr)
	{
		ui->forceServerList = true;
	}

	// The list may still be underneath this screen, in which case closing uncovers it.
	// On a launch that auto selected there is nothing under here but the login screen, and the
	// flag above is what makes that screen show the list rather than skip it again.
	// NativeDestruct drops the game link on the way out, since this server is no longer the choice.
	DeactivateWidget();
}

void UCharacterSelectWidget::handleExitClicked()
{
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	if (subsystem == nullptr)
	{
		UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
		return;
	}

	// The same question the login screen and the in world game menu ask, word for word.
	// Leaving from here costs an account session rather than only a launch, so it earns the asking.
	// Weak, because the answer arrives after the dialog closes and this screen may be gone by then.
	subsystem->askConfirm(LocalizedText::get(TEXT("menu.exit_confirm")), FOnConfirmAnswered::CreateWeakLambda(this, [this](bool confirmed)
	{
		if (confirmed)
		{
			UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
		}
	}));
}

void UCharacterSelectWidget::NativeDestruct()
{
	// Leaving without choosing abandons the session.
	// Handled on destruction rather than deactivation,
	// because the creation screen pushed over this one deactivates it and that is not leaving.
	// The link is dropped rather than left sitting in CharSelect,
	// since a session nobody is driving is one the server would time out regardless.
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;

	if (network != nullptr)
	{
		// The flow outlives this screen, so bindings left behind would aim at a destroyed widget.
		network->gameServer().onDeleteRejected.Unbind();

		// Only the flow is ended here. The next login drops the link itself, and dropping every link from here could cut the login
		// that the Server List button has already started on the screen underneath.
		if (!_chose)
		{
			network->gameServer().end();
		}
	}

	Super::NativeDestruct();
}

UWidget* UCharacterSelectWidget::NativeGetDesiredFocusTarget() const
{
	// A gamepad lands on the way into the world when there is one.
	if ((_enterButton != nullptr) && (_shownObjectId != 0))
	{
		return _enterButton;
	}

	// An account with no characters has exactly one thing to do here, so land on it.
	return _createButton;
}
