#include "UI/NpcDialogWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Core/GameSettings.h"
#include "Kismet/GameplayStatics.h"
#include "ServerConnectionSubsystem.h"
#include "Sound/SoundBase.h"
#include "UI/UIStyle.h"
#include "UI/UISubsystem.h"

namespace
{
	constexpr FLinearColor npcDialogBackdropColor = FLinearColor(0.01f, 0.01f, 0.015f, 0.55f);
	constexpr FLinearColor npcDialogPanelFill = FLinearColor(0.035f, 0.03f, 0.028f, 0.94f);
	constexpr FLinearColor npcDialogTitleColor = FLinearColor(0.86f, 0.68f, 0.36f, 1.0f);

	// What a link's text is when the page gave it no colour, so a link reads as one next to the plain text. Hovering a link brightens its button, as every button of the game does.
	constexpr FLinearColor npcDialogLinkColor = FLinearColor(1.0f, 0.82f, 0.45f, 1.0f);

	constexpr float NPC_DIALOG_WIDTH = 420.0f;
	constexpr float NPC_DIALOG_HEIGHT = 540.0f;
	constexpr float NPC_DIALOG_PAGE_WIDTH = 360.0f;
	constexpr float NPC_DIALOG_INSET = 8.0f;
	constexpr float npcDialogRadius = 8.0f;
	constexpr float npcDialogCloseSize = 40.0f;
	constexpr float npcDialogLineGap = 12.0f;
	constexpr float NPC_DIALOG_ROW_SPACING = 18.0f;
	constexpr float npcDialogLinkRadius = 6.0f;
	constexpr int32 npcDialogFontSize = 18;
	constexpr int32 npcDialogTitleFontSize = 24;

	// The width of an entry box or list whose page gave none, and the height of a multi-line box likewise. A page's own heights are for the stock client's small type.
	constexpr float npcDialogFieldWidth = 140.0f;
	constexpr float npcDialogMultiLineHeight = 80.0f;
	constexpr float npcDialogMultiLineMinHeight = 60.0f;

	// Whether the line is only text, with no link, field or image in it.
	bool npcDialogIsPlain(const NpcHtml::FLine& line)
	{
		for (const NpcHtml::FRun& run : line.runs)
		{
			if ((run.kind != NpcHtml::ERunKind::Text) || (run.linkKind != NpcHtml::ELinkKind::None))
			{
				return false;
			}
		}

		return true;
	}

	// Whether every run of the line is the same colour, so the line can be one wrapped text block.
	bool npcDialogUniformColor(const NpcHtml::FLine& line)
	{
		for (const NpcHtml::FRun& run : line.runs)
		{
			if ((run.hasColor != line.runs[0].hasColor) || (run.hasColor && !run.color.Equals(line.runs[0].color)))
			{
				return false;
			}
		}

		return true;
	}

	FLinearColor npcDialogColorOf(const NpcHtml::FRun& run)
	{
		return run.hasColor ? run.color : UIStyle::labelColor;
	}
}

void UNpcDialogLink::setup(UNpcDialogWidget* dialog, NpcHtml::ELinkKind kind, const FString& target)
{
	_dialog = dialog;
	_kind = kind;
	_target = target;
}

void UNpcDialogLink::handleClicked()
{
	if (_dialog != nullptr)
	{
		_dialog->follow(_kind, _target);
	}
}

TSharedRef<SWidget> UNpcDialogWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		// A faint dim over the running world, with the dialog centred on it.
		UOverlay* root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		UImage* backdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush backdropBrush;
		backdropBrush.TintColor = FSlateColor(npcDialogBackdropColor);
		backdrop->SetBrush(backdropBrush);

		UOverlaySlot* backdropSlot = root->AddChildToOverlay(backdrop);
		backdropSlot->SetHorizontalAlignment(HAlign_Fill);
		backdropSlot->SetVerticalAlignment(VAlign_Fill);

		UScaleBox* fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass());
		fit->SetStretch(EStretch::ScaleToFit);
		fit->SetStretchDirection(EStretchDirection::DownOnly);
		UOverlaySlot* fitSlot = root->AddChildToOverlay(fit);
		fitSlot->SetHorizontalAlignment(HAlign_Center);
		fitSlot->SetVerticalAlignment(VAlign_Center);
		fitSlot->SetPadding(FMargin(16.0f));

		USizeBox* panelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		panelBox->SetWidthOverride(NPC_DIALOG_WIDTH);
		panelBox->SetHeightOverride(NPC_DIALOG_HEIGHT);
		fit->AddChild(panelBox);

		UBorder* panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		panel->SetBrush(UIStyle::stateBrush(npcDialogPanelFill, UIStyle::outlineHovered, npcDialogRadius));
		panel->SetPadding(FMargin(NPC_DIALOG_INSET));
		panelBox->AddChild(panel);

		UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		panel->SetContent(column);

		UBorder* headerFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		headerFrame->SetBrush(UIStyle::stateBrush(FLinearColor(0.105f, 0.065f, 0.035f, 1.0f), UIStyle::outline, npcDialogRadius));
		headerFrame->SetPadding(FMargin(16.0f, 8.0f, 8.0f, 8.0f));
		column->AddChildToVerticalBox(headerFrame);

		UHorizontalBox* header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		headerFrame->SetContent(header);
		_title = UIStyle::makeLabel(*WidgetTree, NSLOCTEXT("NpcDialog", "Title", "Dialogue"), npcDialogTitleFontSize, npcDialogTitleColor);
		_title->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		UHorizontalBoxSlot* titleSlot = header->AddChildToHorizontalBox(_title);
		titleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		titleSlot->SetVerticalAlignment(VAlign_Center);
		titleSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));

		USizeBox* closeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		closeBox->SetWidthOverride(npcDialogCloseSize);
		closeBox->SetHeightOverride(npcDialogCloseSize);

		_closeButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		_closeButton->SetStyle(UIStyle::frontEndButtonStyle(npcDialogLinkRadius));
		_closeButton->OnClicked.AddUniqueDynamic(this, &UNpcDialogWidget::handleCloseClicked);
		UTextBlock* closeLabel = UIStyle::makeLabel(*WidgetTree, FText::FromString(TEXT("X")), npcDialogFontSize, UIStyle::labelColor);
		closeLabel->SetJustification(ETextJustify::Center);
		_closeButton->AddChild(closeLabel);
		closeBox->AddChild(_closeButton);
		header->AddChildToHorizontalBox(closeBox);

		// Only the page scrolls, so the title and close button stay in reach.
		UBorder* page = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		page->SetBrush(UIStyle::stateBrush(FLinearColor(0.022f, 0.019f, 0.017f, 1.0f), UIStyle::outline, npcDialogRadius));
		page->SetPadding(FMargin(16.0f, 16.0f, 10.0f, 12.0f));
		page->SetClipping(EWidgetClipping::ClipToBounds);
		UVerticalBoxSlot* pageSlot = column->AddChildToVerticalBox(page);
		pageSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		pageSlot->SetPadding(FMargin(0.0f, NPC_DIALOG_INSET, 0.0f, 0.0f));

		_content = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		_content->SetScrollbarThickness(FVector2D(6.0f, 6.0f));
		_content->SetScrollbarPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
		_content->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll);
		page->SetContent(_content);

		WidgetTree->RootWidget = root;

		// The click sound lived only in an asset once, so it is loaded here, as the game menu does.
		if (clickSound == nullptr)
		{
			clickSound = LoadObject<USoundBase>(nullptr, UIStyle::clickSoundPath);
		}

		showLines();
	}

	return Super::RebuildWidget();
}

void UNpcDialogWidget::showPage(const FString& html)
{
	_document = NpcHtml::parse(html);
	_hasPage = true;
	showLines();
}

void UNpcDialogWidget::showLines()
{
	if ((_content == nullptr) || (_title == nullptr) || !_hasPage)
	{
		return;
	}

	const FText title = _document.title.IsEmpty() ? NSLOCTEXT("NpcDialog", "Title", "Dialogue") : FText::FromString(_document.title);
	_title->SetText(title);
	_title->SetToolTipText(title);

	_content->ClearChildren();
	_links.Reset();
	_fields.Reset();
	_typing = false;
	for (const NpcHtml::FLine& line : _document.lines)
	{
		addLine(line);
	}

	_content->ScrollToStart();
}

void UNpcDialogWidget::addLine(const NpcHtml::FLine& line)
{
	if (line.runs.IsEmpty())
	{
		USizeBox* gap = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		gap->SetHeightOverride(npcDialogLineGap);
		_content->AddChild(gap);
		return;
	}

	if (npcDialogIsPlain(line) && npcDialogUniformColor(line))
	{
		FString text;
		for (const NpcHtml::FRun& run : line.runs)
		{
			text.Append(run.text);
		}

		UTextBlock* label = UIStyle::makeLabel(*WidgetTree, FText::FromString(text), npcDialogFontSize, npcDialogColorOf(line.runs[0]));
		label->SetAutoWrapText(true);
		label->SetLineHeightPercentage(1.16f);
		label->SetJustification(line.centered ? ETextJustify::Center : ETextJustify::Left);
		UScrollBoxSlot* slot = CastChecked<UScrollBoxSlot>(_content->AddChild(label));
		slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, NPC_DIALOG_ROW_SPACING));
		return;
	}

	// A line that is one link is a button of its own, as wide as the page.
	if ((line.runs.Num() == 1) && (line.runs[0].kind == NpcHtml::ERunKind::Text) && (line.runs[0].linkKind != NpcHtml::ELinkKind::None))
	{
		UButton* button = makeLinkButton(line.runs[0]);
		UButtonSlot* labelSlot = CastChecked<UButtonSlot>(button->GetContent()->Slot);
		labelSlot->SetHorizontalAlignment(line.centered ? HAlign_Center : HAlign_Fill);
		UScrollBoxSlot* slot = CastChecked<UScrollBoxSlot>(_content->AddChild(button));
		slot->SetHorizontalAlignment(HAlign_Fill);
		slot->SetPadding(FMargin(0.0f, 3.0f));
		return;
	}

	// Words and buttons side by side, wrapping where the page runs out. Words are separate blocks so that a line may break between them.
	UWrapBox* row = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass());
	row->SetHorizontalAlignment(line.centered ? HAlign_Center : HAlign_Left);
	for (const NpcHtml::FRun& run : line.runs)
	{
		if (run.kind != NpcHtml::ERunKind::Text)
		{
			row->AddChildToWrapBox(makeElement(run));
			continue;
		}

		if (run.linkKind != NpcHtml::ELinkKind::None)
		{
			row->AddChildToWrapBox(makeLinkButton(run));
			continue;
		}

		TArray<FString> words;
		run.text.ParseIntoArray(words, TEXT(" "), true);
		for (int32 index = 0; index < words.Num(); index++)
		{
			// The space a word was followed by stays with it, since a wrap box puts nothing between its children.
			const bool spaced = (index + 1 < words.Num()) || run.text.EndsWith(TEXT(" "));
			const FString word = spaced ? words[index] + TEXT(" ") : words[index];
			row->AddChildToWrapBox(UIStyle::makeLabel(*WidgetTree, FText::FromString(word), npcDialogFontSize, npcDialogColorOf(run)));
		}
	}

	UScrollBoxSlot* slot = CastChecked<UScrollBoxSlot>(_content->AddChild(row));
	slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, NPC_DIALOG_ROW_SPACING));
}

UButton* UNpcDialogWidget::makeLinkButton(const NpcHtml::FRun& run)
{
	UButton* button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	button->SetStyle(UIStyle::frontEndButtonStyle(npcDialogLinkRadius));

	UTextBlock* label = UIStyle::makeLabel(*WidgetTree, FText::FromString(run.text.TrimStartAndEnd()), npcDialogFontSize, run.hasColor ? run.color : npcDialogLinkColor);
	label->SetAutoWrapText(true);
	button->AddChild(label);

	if (run.linkKind == NpcHtml::ELinkKind::None)
	{
		// A button the page gave no usable action to is shown and does nothing.
		button->SetIsEnabled(false);
		return button;
	}

	UNpcDialogLink* link = NewObject<UNpcDialogLink>(this);
	link->setup(this, run.linkKind, run.linkTarget);
	_links.Add(link);
	button->OnClicked.AddUniqueDynamic(link, &UNpcDialogLink::handleClicked);
	return button;
}

UWidget* UNpcDialogWidget::makeElement(const NpcHtml::FRun& run)
{
	const float width = (run.width > 0.0f) ? FMath::Min(run.width, NPC_DIALOG_PAGE_WIDTH) : npcDialogFieldWidth;
	USizeBox* box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	UWidget* field = nullptr;

	if (run.kind == NpcHtml::ERunKind::Image)
	{
		// The page's pictures are textures of the stock client, which are not used. What is kept is their room, so the page lays out as it was written.
		if ((run.width > 0.0f) || (run.height > 0.0f))
		{
			box->SetWidthOverride(FMath::Min(run.width, NPC_DIALOG_PAGE_WIDTH));
			box->SetHeightOverride(run.height);
		}

		return box;
	}

	box->SetWidthOverride(width);
	if (run.kind == NpcHtml::ERunKind::Edit)
	{
		UEditableTextBox* edit = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		FEditableTextBoxStyle style = UIStyle::frontEndTextBoxStyle(edit->GetWidgetStyle());
		style.TextStyle.SetFont(UIStyle::font(npcDialogFontSize));
		edit->SetWidgetStyle(style);
		field = edit;
	}
	else if (run.kind == NpcHtml::ERunKind::MultiEdit)
	{
		UMultiLineEditableTextBox* edit = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>(UMultiLineEditableTextBox::StaticClass());
		FTextBlockStyle style;
		style.SetFont(UIStyle::font(npcDialogFontSize));
		edit->SetTextStyle(style);
		box->SetHeightOverride((run.height > 0.0f) ? FMath::Max(run.height, npcDialogMultiLineMinHeight) : npcDialogMultiLineHeight);
		field = edit;
	}
	else
	{
		UComboBoxString* list = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass());

		// The list's font is only a property, which the engine marks deprecated for direct access but offers no other way to set after construction.
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		list->Font = UIStyle::font(npcDialogFontSize);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		for (const FString& option : run.options)
		{
			list->AddOption(option);
		}

		list->SetSelectedIndex(0);
		field = list;
	}

	box->AddChild(field);
	if (!run.fieldName.IsEmpty())
	{
		_fields.Add(run.fieldName, field);
	}

	return box;
}

TMap<FString, FString> UNpcDialogWidget::fieldValues() const
{
	TMap<FString, FString> values;
	for (const TPair<FString, TObjectPtr<UWidget>>& field : _fields)
	{
		if (const UEditableTextBox* edit = Cast<UEditableTextBox>(field.Value))
		{
			values.Add(field.Key, edit->GetText().ToString());
		}
		else if (const UMultiLineEditableTextBox* multiLine = Cast<UMultiLineEditableTextBox>(field.Value))
		{
			values.Add(field.Key, multiLine->GetText().ToString());
		}
		else if (const UComboBoxString* list = Cast<UComboBoxString>(field.Value))
		{
			values.Add(field.Key, list->GetSelectedOption());
		}
	}

	return values;
}

void UNpcDialogWidget::follow(NpcHtml::ELinkKind kind, const FString& target)
{
	playClickSound();

	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* connection = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (connection == nullptr)
	{
		return;
	}

	if (kind == NpcHtml::ELinkKind::Bypass)
	{
		connection->world().bypass(NpcHtml::substituteFields(target, fieldValues()));
	}
	else if (kind == NpcHtml::ELinkKind::Page)
	{
		connection->world().link(target);
	}
}

UWidget* UNpcDialogWidget::NativeGetDesiredFocusTarget() const
{
	return _closeButton;
}

TOptional<FUIInputConfig> UNpcDialogWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::All, EMouseCaptureMode::NoCapture);
}

void UNpcDialogWidget::NativeOnFocusChanging(const FWeakWidgetPath& previousFocusPath, const FWidgetPath& newWidgetPath, const FFocusEvent& focusEvent)
{
	Super::NativeOnFocusChanging(previousFocusPath, newWidgetPath, focusEvent);

	bool typing = false;
	for (const TPair<FString, TObjectPtr<UWidget>>& field : _fields)
	{
		const TSharedPtr<SWidget> box = field.Value->GetCachedWidget();
		typing = typing || (box.IsValid() && (field.Value->IsA<UEditableTextBox>() || field.Value->IsA<UMultiLineEditableTextBox>()) && newWidgetPath.ContainsWidget(box.Get()));
	}

	ULocalPlayer* localPlayer = GetOwningLocalPlayer();
	UCommonUIActionRouterBase* router = (localPlayer != nullptr) ? localPlayer->GetSubsystem<UCommonUIActionRouterBase>() : nullptr;
	if ((router != nullptr) && (typing != _typing))
	{
		_typing = typing;
		router->SetActiveUIInputConfig(_typing ? FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture) : GetDesiredInputConfig().GetValue(), this);
	}
}

FReply UNpcDialogWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
	if (keyEvent.GetKey() == EKeys::Escape)
	{
		UGameInstance* gameInstance = GetGameInstance();
		UUISubsystem* ui = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
		if ((ui == nullptr) || ui->acceptEscape(keyEvent.IsRepeat()))
		{
			DeactivateWidget();
		}

		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(geometry, keyEvent);
}

void UNpcDialogWidget::playClickSound() const
{
	if (clickSound == nullptr)
	{
		return;
	}

	const UGameSettings* settings = UGameSettings::get();
	const float volume = (settings != nullptr) ? settings->getEffectiveSfxVolume() : 1.0f;

	UGameplayStatics::PlaySound2D(this, clickSound, volume);
}

void UNpcDialogWidget::handleCloseClicked()
{
	playClickSound();

	// Deactivating pops this off its stack, which is what Escape does too.
	DeactivateWidget();
}
