#include "FrontEnd/CharacterCreateWidget.h"

#include "Appearance.h"
#include "Appearance/AppearanceChoices.h"
#include "Core/GameSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "UI/LocalizedText.h"
#include "ServerConnectionSubsystem.h"
#include "UI/UIStyle.h"
#include "UI/UISubsystem.h"

// Named rather than anonymous, since a unity build puts several of these screens in one file.
// Two anonymous namespaces agreeing on a constant name is then a compile error nobody expects.
namespace characterCreate
{
	// The sex buttons' order, which is also the value _sex holds.
	constexpr uint8 femaleSex = 0;
	constexpr uint8 maleSex = 1;

	// Elves and orcs have eyes of their own to offer past the six colours. Humans do not, and a dwarf has its human donor's.
	bool offersOwnEyes(uint8 species)
	{
		return (species == static_cast<uint8>(Network::EAppearanceSpecies::Elf)) || (species == static_cast<uint8>(Network::EAppearanceSpecies::Orc));
	}

	// The order the race row walks, which the operator set. It is not the order the wire numbers them in, since orcs were stored before dwarves came.
	const Network::EAppearanceSpecies speciesOrder[] = { Network::EAppearanceSpecies::Human, Network::EAppearanceSpecies::Elf, Network::EAppearanceSpecies::Dwarf, Network::EAppearanceSpecies::Orc };
	static_assert(UE_ARRAY_COUNT(speciesOrder) == static_cast<uint8>(Network::EAppearanceSpecies::Count), "Every race has a place in the row.");

	/** The race a step away from this one along the row, wrapping at either end. */
	uint8 stepSpecies(uint8 species, int32 step)
	{
		const int32 count = UE_ARRAY_COUNT(speciesOrder);
		int32 place = 0;
		while ((place < count) && (static_cast<uint8>(speciesOrder[place]) != species))
		{
			place++;
		}

		// A step is one place either way, so adding the count once keeps the index from going negative.
		return static_cast<uint8>(speciesOrder[(place + step + count) % count]);
	}

	const TCHAR* const sexKeys[] =
	{
		TEXT("charcreate.sex_female"),
		TEXT("charcreate.sex_male")
	};

	// The corner column, matching the character screen's stack so the two screens agree.
	// Wider than that screen's, because every row now carries its own name as well as its value.
	constexpr float choiceWidth = 340.0f;
	constexpr float optionSidePad = 90.0f;

	// An arrow either side of a label, for any choice longer than a handful.
	// Sex stays two buttons, because two is a pair rather than a list.
	// Hair is thirty eight, so a button each was never going to fit.
	// Eye colour, hair colour and skin have the same shape, and face will when its spike passes.
	constexpr float arrowWidth = 44.0f;

	// The room a row's own name takes, left of its arrows.
	// Wide enough for MOUSTACHE, which is the longest of them, and which the previous 128 clipped.
	constexpr float captionWidth = 148.0f;

	// The two sex buttons, which are a pair rather than a row and do not want the column's full width.
	// Right aligned against the arrows above them, so the stack still reads as one edge.
	constexpr float sexButtonWidth = 200.0f;

	// How far a dragged pixel turns the avatar, and how far one notch of the wheel zooms.
	// The buttons that used to do both are gone: a player who has ever seen a character screen drags.
	// A quarter of a degree per pixel is a full turn across most of a screen's width.
	// Three degrees of field of view is the resting sixty down to the floor in a dozen notches.
	constexpr float turnDegreesPerPixel = 0.25f;
	constexpr float zoomDegrees = 3.0f;

	// How far the name and its buttons sit off the bottom edge.
	// Lower than the shared control padding, since nothing stands under them here.
	constexpr float nameBottomPad = 30.0f;

	// What a value reads as on a row, letters rather than numbers or words, which the operator asked for.
	// Past Z it doubles up, AA onwards, because the hair has thirty eight of them and the alphabet has not.
	//
	// The word around the letter is translated, the letter itself is not.
	// A letter is a position in a list rather than a word, and every language counts them the same way.
	FText typeName(int32 index)
	{
		FString letters;

		for (int32 left = index; ; left = (left / 26) - 1)
		{
			letters = FString::Chr(static_cast<TCHAR>(TEXT('A') + (left % 26))) + letters;

			if (left < 26)
			{
				break;
			}
		}

		return FText::Format(LocalizedText::get(TEXT("charcreate.value_type")), FText::FromString(letters));
	}

	// The colours and the faces are no longer worded here.
	// Every row shows a letter now, so the only place a hair colour or a face has a name is the applier.
	// One list rather than two that have to agree is also one list that cannot drift.

	// The longest name the game server accepts.
	// It refuses anything longer, so a box that takes a longer one only sets a refusal up.
	constexpr int32 maxNameLength = 16;

	// Sized to the sixteen characters the game server accepts, rather than to the column it sits in.
	// That column is 520 wide and held twenty seven, so the box invited a name it would be refused for.
	// Measured off the body font at 520 for 27, which is a shade over nineteen units a character.
	constexpr float nameBoxWidth = 320.0f;
}

TSharedRef<SWidget> UCharacterCreateWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		// The title owns the top, the controls the bottom, and the character on its spot has the rest.
		// A scrim would have covered the very thing the camera moved here to show.
		UOverlay* root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		UImage* wash = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush washBrush;
		washBrush.TintColor = FSlateColor(UIStyle::frontEndWashColor);
		wash->SetBrush(washBrush);

		UOverlaySlot* washSlot = root->AddChildToOverlay(wash);
		washSlot->SetHorizontalAlignment(HAlign_Fill);
		washSlot->SetVerticalAlignment(VAlign_Fill);

		// No title.
		// The character on its spot says what this screen is.
		// A caption over their head only competes with them for the top of the frame.
		// The charcreate.title string stays in the table, since the select screen borrows it.
		// That screen shows it as the prompt for an account with nobody on its spot.
		UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		USizeBox* panel = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		panel->SetWidthOverride(UIStyle::frontEndColumnWidth);
		panel->AddChild(column);

		UOverlaySlot* panelSlot = root->AddChildToOverlay(panel);
		panelSlot->SetHorizontalAlignment(HAlign_Center);
		panelSlot->SetVerticalAlignment(VAlign_Bottom);
		panelSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, characterCreate::nameBottomPad));

		// Sex sits in the bottom right corner rather than above the name, on its own, because the
		// rest of that corner is where hair, skin and the other choices go once they exist.
		UVerticalBox* options = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		UButton* previousSpecies = nullptr;
		UButton* nextSpecies = nullptr;
		_speciesLabel = addCycler(*options, LocalizedText::get(TEXT("charcreate.row_species")), previousSpecies, nextSpecies);
		previousSpecies->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleSpeciesPrevClicked);
		nextSpecies->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleSpeciesNextClicked);

		UButton* headPrevious = nullptr;
		UButton* headNext = nullptr;
		_headLabel = addCycler(*options, LocalizedText::get(TEXT("charcreate.row_face")), headPrevious, headNext);
		headPrevious->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleHeadPrevClicked);
		headNext->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleHeadNextClicked);

		UButton* skinPrevious = nullptr;
		UButton* skinNext = nullptr;
		_skinToneLabel = addCycler(*options, LocalizedText::get(TEXT("charcreate.row_tone")), skinPrevious, skinNext);
		skinPrevious->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleSkinPrevClicked);
		skinNext->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleSkinNextClicked);

		UButton* eyesPrevious = nullptr;
		UButton* eyesNext = nullptr;
		_eyeColorLabel = addCycler(*options, LocalizedText::get(TEXT("charcreate.row_eyes")), eyesPrevious, eyesNext);
		eyesPrevious->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleEyesPrevClicked);
		eyesNext->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleEyesNextClicked);

		UButton* hairPrevious = nullptr;
		UButton* hairNext = nullptr;
		_hairStyleLabel = addCycler(*options, LocalizedText::get(TEXT("charcreate.row_hair")), hairPrevious, hairNext);
		hairPrevious->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleHairPrevClicked);
		hairNext->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleHairNextClicked);

		// Named COLOR rather than HAIR COLOR, since it sits directly under the hair.
		// Nothing else on the column is a colour any more, the facial hair having lost its own row.
		UButton* hairColorPrevious = nullptr;
		UButton* hairColorNext = nullptr;
		_hairColorLabel = addCycler(*options, LocalizedText::get(TEXT("charcreate.row_hair_color")), hairColorPrevious, hairColorNext);
		hairColorPrevious->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleHairColorPrevClicked);
		hairColorNext->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleHairColorNextClicked);

		UButton* eyebrowsPrevious = nullptr;
		UButton* eyebrowsNext = nullptr;
		_eyebrowsLabel = addCycler(*options, LocalizedText::get(TEXT("charcreate.row_eyebrows")), eyebrowsPrevious, eyebrowsNext);
		eyebrowsPrevious->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleEyebrowsPrevClicked);
		eyebrowsNext->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleEyebrowsNextClicked);

		UButton* beardPrevious = nullptr;
		UButton* beardNext = nullptr;
		_facialHairLabel = addCycler(*options, LocalizedText::get(TEXT("charcreate.row_beard")), beardPrevious, beardNext);
		beardPrevious->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleBeardPrevClicked);
		beardNext->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleBeardNextClicked);

		UButton* mustachePrevious = nullptr;
		UButton* mustacheNext = nullptr;
		_mustacheLabel = addCycler(*options, LocalizedText::get(TEXT("charcreate.row_moustache")), mustachePrevious, mustacheNext);
		mustachePrevious->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleMustachePrevClicked);
		mustacheNext->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleMustacheNextClicked);

		addChoice(*options, characterCreate::sexKeys[0], _sexButtons)->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleFemaleClicked);
		addChoice(*options, characterCreate::sexKeys[1], _sexButtons)->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleMaleClicked);

		USizeBox* optionBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		optionBox->SetWidthOverride(characterCreate::choiceWidth);
		optionBox->AddChild(options);

		UOverlaySlot* optionSlot = root->AddChildToOverlay(optionBox);
		optionSlot->SetHorizontalAlignment(HAlign_Right);
		optionSlot->SetVerticalAlignment(VAlign_Bottom);
		optionSlot->SetPadding(FMargin(0.0f, 0.0f, characterCreate::optionSidePad, UIStyle::frontEndControlsBottomPad));

		_nameBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		_nameBox->SetWidgetStyle(UIStyle::frontEndTextBoxStyle(_nameBox->GetWidgetStyle()));

		// Centred, since the box is now sized to a full sixteen characters and most names are shorter.
		// Left justified, a short one sat against the edge of a box with nothing in the rest of it.
		_nameBox->SetJustification(ETextJustify::Center);
		_nameBox->OnTextChanged.AddUniqueDynamic(this, &UCharacterCreateWidget::handleNameChanged);
		_nameBox->OnTextCommitted.AddUniqueDynamic(this, &UCharacterCreateWidget::handleNameCommitted);

		// The box gets its own width rather than the column's.
		// That column also carries the message line and the two buttons, which want the wider room.
		USizeBox* nameBoxWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		nameBoxWidth->SetWidthOverride(characterCreate::nameBoxWidth);
		nameBoxWidth->AddChild(_nameBox);

		UVerticalBoxSlot* nameSlot = column->AddChildToVerticalBox(nameBoxWidth);
		nameSlot->SetHorizontalAlignment(HAlign_Center);

		_messageText = UIStyle::makeLabel(*WidgetTree, FText::GetEmpty(), UIStyle::bodyFontSize, UIStyle::mutedColor);

		UVerticalBoxSlot* messageSlot = column->AddChildToVerticalBox(_messageText);
		messageSlot->SetHorizontalAlignment(HAlign_Center);
		messageSlot->SetPadding(FMargin(0.0f, UIStyle::rowSpacing, 0.0f, 0.0f));

		UHorizontalBox* buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		_createButton = UIStyle::frontEndButton(*WidgetTree, LocalizedText::get(TEXT("menu.create")));
		_createButton->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleCreateClicked);
		buttons->AddChildToHorizontalBox(_createButton)->SetPadding(FMargin(0.0f, 0.0f, UIStyle::rowSpacing, 0.0f));

		_backButton = UIStyle::frontEndButton(*WidgetTree, LocalizedText::get(TEXT("menu.back")));
		_backButton->OnClicked.AddUniqueDynamic(this, &UCharacterCreateWidget::handleBackClicked);
		buttons->AddChildToHorizontalBox(_backButton);

		UVerticalBoxSlot* buttonSlot = column->AddChildToVerticalBox(buttons);
		buttonSlot->SetHorizontalAlignment(HAlign_Center);
		buttonSlot->SetPadding(FMargin(0.0f, UIStyle::rowSpacing * 2.0f, 0.0f, 0.0f));

		// Last, because it lights whichever buttons the defaults chose and they have to exist first.
		refreshChoices();

		WidgetTree->RootWidget = root;
	}

	return Super::RebuildWidget();
}

void UCharacterCreateWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	setMessage(FText::GetEmpty());

	// A screen reached a second time is a fresh one, though the widget behind it is not.
	// The stack keeps this object alive between visits, so what the last visit left is still here.
	// That is the name it created, and a Create button switched off to stop a second press mid request.
	// Together they made a second character impossible until 2026-08-13.
	if (_nameBox != nullptr)
	{
		_nameBox->SetText(FText::GetEmpty());
	}

	if (_createButton != nullptr)
	{
		_createButton->SetIsEnabled(true);
	}

	// Whoever the toggle currently says, which on a fresh screen is the female default.
	// Standing them up here also removes the character the select screen left on its spot.
	showAvatar();

	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (network == nullptr)
	{
		return;
	}

	// These two belong to this screen alone, which is why the flow carries them apart from the step delegate the login screen holds.
	// Closing is all a success owes. The server sends the list again, and the login screen hands it to the select screen underneath.
	network->gameServer().onCreated.BindWeakLambda(this, [this]()
	{
		// The one just made is the one the player wants to be looking at. Its id is not known yet, so the select screen is told its name.
		UGameInstance* createdInstance = GetGameInstance();
		UUISubsystem* ui = (createdInstance != nullptr) ? createdInstance->GetSubsystem<UUISubsystem>() : nullptr;
		if (ui != nullptr)
		{
			ui->createdName = _submittedName;
		}

		DeactivateWidget();
	});

	network->gameServer().onCreateRejected.BindWeakLambda(this, [this](Network::ECreateRejectReason reason)
	{
		// Developer wording, since none of these have string table entries yet.
		setMessage(FText::FromString(FString::Printf(TEXT("Refused: %s."), Network::createRejectReasonName(reason))));

		if (_createButton != nullptr)
		{
			_createButton->SetIsEnabled(true);
		}
	});
}

void UCharacterCreateWidget::NativeOnDeactivated()
{
	// The camera goes back to the framing it was given before this screen narrowed it.
	// Zero is the word for that.
	// Leaving it narrowed would hand the next screen a stop the operator never set.
	askFrontEnd([](UUISubsystem& ui)
	{
		if (ui.zoomView)
		{
			ui.zoomView(0.0f);
		}
	});

	// The flow outlives this screen, so bindings left behind would aim at a destroyed widget.
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (network != nullptr)
	{
		network->gameServer().onCreated.Unbind();
		network->gameServer().onCreateRejected.Unbind();
	}

	Super::NativeOnDeactivated();
}

void UCharacterCreateWidget::submit()
{
	const FString name = (_nameBox != nullptr) ? _nameBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (name.IsEmpty())
	{
		setMessage(LocalizedText::get(TEXT("charcreate.name_required")));
		return;
	}

	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (network == nullptr)
	{
		return;
	}

	_submittedName = name;
	if (!network->gameServer().createCharacter(name, chosenAppearance()))
	{
		setMessage(FText::FromString(TEXT("The session is not ready for this.")));
		return;
	}

	setMessage(LocalizedText::get(TEXT("charcreate.working")));

	if (_createButton != nullptr)
	{
		// A second press cannot ask twice while one request is outstanding.
		_createButton->SetIsEnabled(false);
	}
}

void UCharacterCreateWidget::setMessage(const FText& text)
{
	if (_messageText != nullptr)
	{
		_messageText->SetText(text);
	}
}

void UCharacterCreateWidget::handleCreateClicked()
{
	submit();
}

void UCharacterCreateWidget::handleBackClicked()
{
	DeactivateWidget();
}

void UCharacterCreateWidget::handleNameChanged(const FText& text)
{
	// UEditableTextBox has no length of its own to set, so the cap is kept by cutting what arrives.
	// Writing the text back fires this again, and the shorter string passes, so it settles at once.
	if ((_nameBox == nullptr) || (text.ToString().Len() <= characterCreate::maxNameLength))
	{
		return;
	}

	_nameBox->SetText(FText::FromString(text.ToString().Left(characterCreate::maxNameLength)));
}

void UCharacterCreateWidget::handleNameCommitted(const FText& text, ETextCommit::Type commitMethod)
{
	// Enter submits, which is what a single field screen has taught everyone to expect.
	if (commitMethod == ETextCommit::OnEnter)
	{
		submit();
	}
}

UButton* UCharacterCreateWidget::addChoice(UVerticalBox& column, const TCHAR* key, TArray<TObjectPtr<UButton>>& into)
{
	UButton* button = UIStyle::frontEndButton(*WidgetTree, LocalizedText::get(key));

	// Narrower than the column, and only in width, since the rows above set the height everything shares.
	USizeBox* buttonBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	buttonBox->SetWidthOverride(characterCreate::sexButtonWidth);
	buttonBox->AddChild(button);

	UVerticalBoxSlot* slot = column.AddChildToVerticalBox(buttonBox);
	slot->SetHorizontalAlignment(HAlign_Right);
	slot->SetPadding(FMargin(0.0f, UIStyle::rowSpacing, 0.0f, 0.0f));

	// The array order is the design's numbering, which is how a click knows which byte it means.
	into.Add(button);

	return button;
}

UTextBlock* UCharacterCreateWidget::addCycler(UVerticalBox& column, const FText& captionText, UButton*& outPrevious, UButton*& outNext)
{
	UHorizontalBox* row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	// What the row is, outside the arrows and left of them, so the value between them can be a bare word.
	// Developer wording rather than a string table entry, as the reject reasons and the colours are.
	// The same near white the values wear, rimmed, because this one stands on the scene itself.
	// Muted grey is a fine second voice against a dark panel and unreadable against a lit scene.
	// The rim alone was not enough, so the colour went with it.
	UTextBlock* caption = UIStyle::makeLabel(*WidgetTree, captionText, UIStyle::frontEndButtonFontSize, UIStyle::labelColor);
	caption->SetJustification(ETextJustify::Left);
	caption->SetFont(UIStyle::outlinedFont(UIStyle::frontEndButtonFontSize));

	USizeBox* captionBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	captionBox->SetWidthOverride(characterCreate::captionWidth);
	captionBox->AddChild(caption);

	UHorizontalBoxSlot* captionSlot = row->AddChildToHorizontalBox(captionBox);
	captionSlot->SetVerticalAlignment(VAlign_Center);

	// The arrows carry no localised text, since an arrow is the same glyph everywhere.
	outPrevious = UIStyle::frontEndButton(*WidgetTree, FText::FromString(TEXT("<")));

	USizeBox* previousBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	previousBox->SetWidthOverride(characterCreate::arrowWidth);
	previousBox->AddChild(outPrevious);
	row->AddChildToHorizontalBox(previousBox);

	UTextBlock* label = UIStyle::makeLabel(*WidgetTree, FText::GetEmpty(), UIStyle::frontEndButtonFontSize, UIStyle::labelColor);
	label->SetJustification(ETextJustify::Center);

	// Rimmed like the caption, since this stands on the scene between two buttons rather than on one.
	label->SetFont(UIStyle::outlinedFont(UIStyle::frontEndButtonFontSize));

	// The label takes what the two arrows leave, so both rows line up whatever the caption says.
	UHorizontalBoxSlot* labelSlot = row->AddChildToHorizontalBox(label);
	labelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	labelSlot->SetHorizontalAlignment(HAlign_Fill);
	labelSlot->SetVerticalAlignment(VAlign_Center);

	outNext = UIStyle::frontEndButton(*WidgetTree, FText::FromString(TEXT(">")));

	USizeBox* nextBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	nextBox->SetWidthOverride(characterCreate::arrowWidth);
	nextBox->AddChild(outNext);
	row->AddChildToHorizontalBox(nextBox);

	UVerticalBoxSlot* rowSlot = column.AddChildToVerticalBox(row);
	rowSlot->SetHorizontalAlignment(HAlign_Fill);
	rowSlot->SetPadding(FMargin(0.0f, UIStyle::rowSpacing, 0.0f, 0.0f));

	return label;
}

// Wraps rather than stopping at the ends.
// A list this short is quicker to circle than to walk back along.
// An arrow that does nothing at the end reads as a broken button rather than as a boundary.
uint8 UCharacterCreateWidget::cycle(uint8 value, int32 step, uint8 count)
{
	if (count == 0)
	{
		return 0;
	}

	const int32 wrapped = (static_cast<int32>(value) + step + static_cast<int32>(count)) % static_cast<int32>(count);

	return static_cast<uint8>(wrapped);
}

void UCharacterCreateWidget::refreshChoices()
{
	for (int32 index = 0; index < _sexButtons.Num(); index++)
	{
		_sexButtons[index]->SetStyle(UIStyle::frontEndToggleStyle(index == static_cast<int32>(_sex)));
	}

	// Lettered rather than named, on the operator's instruction of 2026-08-13.
	// The row says what it is, so the value only has to say which one, and every row reads alike.
	// Worded rather than lettered, unlike every other row.
	// A face has no name a player should read, but a colour does and everybody already knows it.
	// The order is the applier's table, which is the only thing that makes a name true.
	if (_eyeColorLabel != nullptr)
	{
		const TCHAR* const eyeColourKeys[] =
		{
			TEXT("charcreate.eye_brown"),
			TEXT("charcreate.eye_amber"),
			TEXT("charcreate.eye_green"),
			TEXT("charcreate.eye_blue"),
			TEXT("charcreate.eye_grey"),
			TEXT("charcreate.eye_black")
		};

		static_assert(UE_ARRAY_COUNT(eyeColourKeys) == AppearanceChoices::eyeColors, "Every eye colour the wire allows needs a word on the button.");

		const uint8 choice = _eyeColors[_speciesIndex];
		const TCHAR* key = (characterCreate::offersOwnEyes(_speciesIndex) && (choice == AppearanceChoices::donorEyeColor)) ? TEXT("options.defaults") : eyeColourKeys[choice % AppearanceChoices::eyeColors];
		_eyeColorLabel->SetText(LocalizedText::get(key));
	}

	if (_hairStyleLabel != nullptr)
	{
		const uint8 style = chosenHairStyle();
		if (style == AppearanceChoices::baldHairStyle)
		{
			_hairStyleLabel->SetText(LocalizedText::get(TEXT("charcreate.hair_bald")));
		}
		else
		{
			_hairStyleLabel->SetText(characterCreate::typeName(style));
		}
	}

	// Worded rather than lettered, as the eyes are, since a colour has a name and a letter hides it.
	// The order is the applier's melanin and redness table, which is what makes each word true.
	if (_hairColorLabel != nullptr)
	{
		const TCHAR* const hairColourKeys[] =
		{
			TEXT("charcreate.hair_brown"),
			TEXT("charcreate.hair_black"),
			TEXT("charcreate.hair_auburn"),
			TEXT("charcreate.hair_red"),
			TEXT("charcreate.hair_blonde")
		};

		static_assert(UE_ARRAY_COUNT(hairColourKeys) == AppearanceChoices::hairColors, "Every hair colour the wire allows needs a word on the button.");

		_hairColorLabel->SetText(LocalizedText::get(hairColourKeys[(_hairColors[_speciesIndex]) % AppearanceChoices::hairColors]));
	}

	// Offered to men alone, so the row leaves rather than greys out.
	// A disabled row is a promise that something will enable it, and nothing here will.
	// The row is the label's parent, since that is what addCycler put the arrows in either side of.
	if (_facialHairLabel != nullptr)
	{
		const bool male = (_sex == characterCreate::maleSex);

		if (UWidget* row = _facialHairLabel->GetParent())
		{
			row->SetVisibility(male ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}

		// Zero is a clean chin rather than the first beard, so the wording splits at zero too.
		_facialHairLabel->SetText((_facialHair == 0)
			? LocalizedText::get(TEXT("charcreate.value_none"))
			: characterCreate::typeName(_facialHair - 1));
	}

	// The same rule as the beard, since the two are worn together and hidden together.
	if (_mustacheLabel != nullptr)
	{
		const bool male = (_sex == characterCreate::maleSex);

		if (UWidget* row = _mustacheLabel->GetParent())
		{
			row->SetVisibility(male ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}

		// Zero is a bare lip rather than the first moustache, so the wording splits at zero too.
		_mustacheLabel->SetText((_mustache == 0)
			? LocalizedText::get(TEXT("charcreate.value_none"))
			: characterCreate::typeName(_mustache - 1));
	}

	// Lettered like everything else, rather than by the name of the MetaHuman preset behind it.
	// Those names are ours to know and a player's to never see, and they change when a face is swapped.
	if (_eyebrowsLabel != nullptr)
	{
		_eyebrowsLabel->SetText(characterCreate::typeName(chosenEyebrows() % AppearanceChoices::eyebrows));
	}

	if (_speciesLabel != nullptr)
	{
		static const TCHAR* const keys[] = { TEXT("charcreate.species_human"), TEXT("charcreate.species_elf"), TEXT("charcreate.species_orc"), TEXT("charcreate.species_dwarf") };
		static_assert(UE_ARRAY_COUNT(keys) == speciesCount);
		_speciesLabel->SetText(LocalizedText::get(keys[_speciesIndex]));
	}

	if (_headLabel != nullptr)
	{
		_headLabel->SetText(characterCreate::typeName(_head % AppearanceChoices::heads));
	}

	// Lettered rather than named on purpose.
	// Skin tone names carry meaning a letter does not, and those words want choosing deliberately.
	// The order still runs lightest to darkest, which the letters say as well as numbers did.
	if (_skinToneLabel != nullptr)
	{
		_skinToneLabel->SetText(characterCreate::typeName(_skinTones[_speciesIndex]));
	}
}

void UCharacterCreateWidget::handleSpeciesPrevClicked()
{
	_speciesIndex = characterCreate::stepSpecies(_speciesIndex, -1);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleSpeciesNextClicked()
{
	_speciesIndex = characterCreate::stepSpecies(_speciesIndex, 1);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleFemaleClicked()
{
	_sex = characterCreate::femaleSex;
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleMaleClicked()
{
	_sex = characterCreate::maleSex;
	refreshChoices();
	showAvatar();
}

// Four of these rather than two with an argument, since OnClicked carries no payload.
// The same reason the two sex buttons have one each.
void UCharacterCreateWidget::handleEyesPrevClicked()
{
	uint8& colour = _eyeColors[_speciesIndex];
	colour = cycle(colour, -1, characterCreate::offersOwnEyes(_speciesIndex) ? AppearanceChoices::eyeColors + 1 : AppearanceChoices::eyeColors);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleEyesNextClicked()
{
	uint8& colour = _eyeColors[_speciesIndex];
	colour = cycle(colour, 1, characterCreate::offersOwnEyes(_speciesIndex) ? AppearanceChoices::eyeColors + 1 : AppearanceChoices::eyeColors);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleHairPrevClicked()
{
	cycleHair(-1);
}

void UCharacterCreateWidget::handleHairNextClicked()
{
	cycleHair(1);
}

uint8 UCharacterCreateWidget::chosenHairStyle() const
{
	if (_speciesIndex == 0)
	{
		return _hairStyle;
	}

	// Elf face A starts with the low ponytail or casual side part until the player chooses a style.
	const uint8 firstFaceStyle = _sex == characterCreate::maleSex ? 28 : 12;
	return _speciesHairStyles[_speciesIndex][_sex].Get(_speciesIndex == static_cast<uint8>(Network::EAppearanceSpecies::Elf) && _head == 0 ? firstFaceStyle : uint8(0));
}

void UCharacterCreateWidget::cycleHair(int32 step)
{
	const uint8 count = AppearanceChoices::hairStyles + 1;
	const uint8 style = cycle(chosenHairStyle(), step, count);
	if (_speciesIndex != 0)
	{
		_speciesHairStyles[_speciesIndex][_sex] = style;
	}
	else
	{
		_hairStyle = style;
	}

	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleHairColorPrevClicked()
{
	uint8& colour = _hairColors[_speciesIndex];
	colour = cycle(colour, -1, AppearanceChoices::hairColors);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleHairColorNextClicked()
{
	uint8& colour = _hairColors[_speciesIndex];
	colour = cycle(colour, 1, AppearanceChoices::hairColors);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleBeardPrevClicked()
{
	_facialHair = cycle(_facialHair, -1, AppearanceChoices::beards);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleBeardNextClicked()
{
	_facialHair = cycle(_facialHair, 1, AppearanceChoices::beards);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleMustachePrevClicked()
{
	_mustache = cycle(_mustache, -1, AppearanceChoices::mustaches);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleMustacheNextClicked()
{
	_mustache = cycle(_mustache, 1, AppearanceChoices::mustaches);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleHeadPrevClicked()
{
	_head = cycle(_head, -1, AppearanceChoices::heads);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleHeadNextClicked()
{
	_head = cycle(_head, 1, AppearanceChoices::heads);
	refreshChoices();
	showAvatar();
}

Network::FAppearance UCharacterCreateWidget::chosenAppearance() const
{
	// One place, since the preview on its spot and the stored character are one person.
	// Two call sites building this separately is how they stop being.
	Network::FAppearance appearance;
	appearance.male = (_sex == characterCreate::maleSex);
	appearance.species = static_cast<Network::EAppearanceSpecies>(_speciesIndex);
	appearance.head = _head;
	appearance.skinTone = _skinTones[_speciesIndex];
	appearance.eyeColor = _eyeColors[_speciesIndex];
	appearance.hairStyle = chosenHairStyle();
	appearance.hairColor = _hairColors[_speciesIndex];
	appearance.eyebrows = chosenEyebrows();

	// Zero for a woman whatever the rows were left on, since the rows are not hers to have set.
	appearance.beard = appearance.male ? _facialHair : 0;
	appearance.mustache = appearance.male ? _mustache : 0;

	return appearance;
}

TArray<uint8> UCharacterCreateWidget::packAppearance() const
{
	return chosenAppearance().pack();
}

void UCharacterCreateWidget::handleEyebrowsPrevClicked()
{
	cycleEyebrows(-1);
}

void UCharacterCreateWidget::handleEyebrowsNextClicked()
{
	cycleEyebrows(1);
}

uint8 UCharacterCreateWidget::chosenEyebrows() const
{
	if (_speciesIndex == 0)
	{
		return _eyebrows;
	}

	// The brow styles follow the four original human faces for each sex.
	static constexpr uint8 donorBrows[][AppearanceChoices::heads] = { { 16, 1, 16, 1 }, { 6, 8, 9, 13 } };
	return _speciesEyebrows[_speciesIndex][_sex].Get(donorBrows[_sex][_head]);
}

void UCharacterCreateWidget::cycleEyebrows(int32 step)
{
	const uint8 brows = cycle(chosenEyebrows(), step, AppearanceChoices::eyebrows);
	if (_speciesIndex != 0)
	{
		_speciesEyebrows[_speciesIndex][_sex] = brows;
	}
	else
	{
		_eyebrows = brows;
	}

	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleSkinPrevClicked()
{
	uint8& tone = _skinTones[_speciesIndex];
	tone = cycle(tone, -1, AppearanceChoices::skinTones);
	refreshChoices();
	showAvatar();
}

void UCharacterCreateWidget::handleSkinNextClicked()
{
	uint8& tone = _skinTones[_speciesIndex];
	tone = cycle(tone, 1, AppearanceChoices::skinTones);
	refreshChoices();
	showAvatar();
}

// Held left button drags the avatar round, and the wheel walks the field of view in and out.
// The two rows of arrows that used to do this are gone, on the operator's instruction of 2026-08-13.
//
// The mouse is caught on the way down rather than watched, so a drag that leaves the screen still ends here.
// Only the empty part of the screen reaches this at all: a button under the cursor answers first.
FReply UCharacterCreateWidget::NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
	if (mouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(geometry, mouseEvent);
	}

	_dragging = true;

	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UCharacterCreateWidget::NativeOnMouseButtonUp(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
	if (mouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonUp(geometry, mouseEvent);
	}

	_dragging = false;

	return FReply::Handled().ReleaseMouseCapture();
}

FReply UCharacterCreateWidget::NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
	if (!_dragging)
	{
		return Super::NativeOnMouseMove(geometry, mouseEvent);
	}

	// Sideways only. Turning a character on its side is a thing nobody wants and every screen refuses.
	const float across = mouseEvent.GetCursorDelta().X;

	askFrontEnd([across](UUISubsystem& ui)
	{
		if (ui.turnAvatar)
		{
			// Negated, so the body follows the hand: dragging right brings its right side round to the front.
			ui.turnAvatar(-across * characterCreate::turnDegreesPerPixel);
		}
	});

	return FReply::Handled();
}

FReply UCharacterCreateWidget::NativeOnMouseWheel(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
	const float notches = mouseEvent.GetWheelDelta();

	askFrontEnd([notches](UUISubsystem& ui)
	{
		if (ui.zoomView)
		{
			// Negative narrows, since a narrower field of view is a closer look, and up the wheel is closer.
			ui.zoomView(-notches * characterCreate::zoomDegrees);
		}
	});

	return FReply::Handled();
}

void UCharacterCreateWidget::askFrontEnd(TFunctionRef<void(UUISubsystem&)> ask) const
{
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* ui = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;

	if (ui != nullptr)
	{
		ask(*ui);
	}
}

void UCharacterCreateWidget::showAvatar()
{
	// The body on its spot belongs to the front end game mode, which is what sees the level.
	// The hook is unset on any level with no front end, so this asks before it calls.
	askFrontEnd([this](UUISubsystem& ui)
	{
		if (ui.showAvatar)
		{
			// Packed here rather than sent field by field.
			// The preview and the created character are then built from the very same bytes.
			ui.showAvatar(EAvatarSpot::Create, packAppearance(), TConstArrayView<int32>());
		}
	});
}

UWidget* UCharacterCreateWidget::NativeGetDesiredFocusTarget() const
{
	// Typing should start in the name field rather than on a button.
	if (_nameBox != nullptr)
	{
		return _nameBox;
	}

	return _createButton;
}
