#include "FrontEnd/ServerSelectWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Core/GameSettings.h"
#include "UI/LocalizedText.h"
#include "ServerConnectionSubsystem.h"
#include "UI/UIStyle.h"

// Named rather than anonymous, for the reason the character screen gives: the module builds unity
// and statusText is exactly the name a second screen reaches for.
namespace serverSelect
{
	// How tall the list may grow before it scrolls instead.
	// The number of servers is the operator's decision, so the screen cannot assume a small one.
	constexpr float listMaxHeight = 520.0f;

	// What buttonStyle puts around a button's content, horizontally.
	// Named here because the row inside needs its own width and has to subtract this to get it.
	constexpr float rowButtonPadding = 12.0f;

	// Matching the character screen's corner stack, since both are the way out of their screen.
	constexpr float backButtonWidth = 200.0f;
	constexpr float backSidePad = 90.0f;

	// The status word is a key rather than the developer name the network layer prints,
	// because that one is for a log and this one is read by a player.
	FText statusText(Network::EServerStatus status)
	{
		return LocalizedText::get((status == Network::EServerStatus::Online) ? TEXT("server.status_online") : TEXT("server.status_offline"));
	}
}

void UServerRowHandler::handleClicked()
{
	if (owner != nullptr)
	{
		owner->chooseServer(serverId);
	}
}

TSharedRef<SWidget> UServerSelectWidget::RebuildWidget()
{
	// Built here rather than in NativeConstruct.
	// This is the one place a native widget is guaranteed to have a tree to build into.
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		// The same shape as the character screen: title on top, the choice in the middle,
		// and the way out in the bottom corner. A scrim would have hidden the scene behind it.
		UOverlay* root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		UImage* wash = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush washBrush;
		washBrush.TintColor = FSlateColor(UIStyle::frontEndWashColor);
		wash->SetBrush(washBrush);

		UOverlaySlot* washSlot = root->AddChildToOverlay(wash);
		washSlot->SetHorizontalAlignment(HAlign_Fill);
		washSlot->SetVerticalAlignment(VAlign_Fill);

		UTextBlock* title = UIStyle::makeLabel(*WidgetTree, LocalizedText::get(TEXT("server.title")), UIStyle::frontEndTitleFontSize, UIStyle::frontEndTitleColor);
		FSlateFontInfo titleFont = UIStyle::font(UIStyle::frontEndTitleFontSize);
		titleFont.LetterSpacing = UIStyle::frontEndTitleLetterSpacing;
		title->SetFont(titleFont);
		title->SetJustification(ETextJustify::Center);

		UOverlaySlot* titleSlot = root->AddChildToOverlay(title);
		titleSlot->SetHorizontalAlignment(HAlign_Center);
		titleSlot->SetVerticalAlignment(VAlign_Top);
		titleSlot->SetPadding(FMargin(0.0f, UIStyle::frontEndTitleTopPad, 0.0f, 0.0f));

		_rows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		// Scrollable, because the number of servers is the operator's decision and not ours.
		// Eighty of them used to run off the top and bottom of the screen with no way to reach
		// the ones in between, which is a list that lies about what it is offering.
		UScrollBox* scroller = UIStyle::makeScrollBox(*WidgetTree);
		scroller->AddChild(_rows);

		USizeBox* panel = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		panel->SetWidthOverride(UIStyle::panelWidth);
		panel->SetMaxDesiredHeight(serverSelect::listMaxHeight);
		panel->AddChild(scroller);

		UOverlaySlot* panelSlot = root->AddChildToOverlay(panel);
		panelSlot->SetHorizontalAlignment(HAlign_Center);
		panelSlot->SetVerticalAlignment(VAlign_Center);

		_backButton = UIStyle::frontEndButton(*WidgetTree, LocalizedText::get(TEXT("menu.back")));
		_backButton->OnClicked.AddUniqueDynamic(this, &UServerSelectWidget::handleBackClicked);

		USizeBox* backBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		backBox->SetWidthOverride(serverSelect::backButtonWidth);
		backBox->AddChild(_backButton);

		UOverlaySlot* backSlot = root->AddChildToOverlay(backBox);
		backSlot->SetHorizontalAlignment(HAlign_Right);
		backSlot->SetVerticalAlignment(VAlign_Bottom);
		backSlot->SetPadding(FMargin(0.0f, 0.0f, serverSelect::backSidePad, UIStyle::frontEndControlsBottomPad));

		WidgetTree->RootWidget = root;
	}

	return Super::RebuildWidget();
}

void UServerSelectWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// Filled on activation rather than on construction.
	// The list arrives from the login server after this class is known and before it is shown.
	populate();

	// Reset because this is once again a screen nobody has chosen from.
	_chose = false;
}

void UServerSelectWidget::populate()
{
	if ((_rows == nullptr) || (WidgetTree == nullptr))
	{
		return;
	}

	_rows->ClearChildren();
	_handlers.Reset();
	_firstRow = nullptr;

	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (network == nullptr)
	{
		return;
	}

	const TArray<Network::FServerListEntry>& servers = network->loginServer().servers();

	if (servers.Num() == 0)
	{
		_rows->AddChildToVerticalBox(UIStyle::makeLabel(*WidgetTree, LocalizedText::get(TEXT("server.none")), UIStyle::bodyFontSize, UIStyle::mutedColor));
		return;
	}

	for (const Network::FServerListEntry& entry : servers)
	{
		const bool selectable = (entry.status == Network::EServerStatus::Online);
		const FLinearColor colour = selectable ? UIStyle::labelColor : UIStyle::mutedColor;

		// An overlay rather than a horizontal box, and a spacer rather than a fill.
		// Two attempts at making the status reach the right edge failed the same way: the row kept
		// shrink wrapping to its labels inside a seven hundred unit button, so neither HAlign_Fill on
		// the button slot nor a SizeBox around the row ever gave it a width to divide.
		// An overlay takes the largest of its children as its own size, so the spacer below decides
		// that size outright and the two labels hang off opposite edges of it.
		// Nothing here depends on how a parent chooses to stretch a child, which is what broke twice.
		UOverlay* row = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		USpacer* width = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
		width->SetSize(FVector2D(UIStyle::panelWidth - (2.0f * serverSelect::rowButtonPadding), 0.0f));
		row->AddChildToOverlay(width);

		// Filled and justified rather than aligned. The overlay is the right width now, measured:
		// the status landed dead centre of it to the pixel, which is what an alignment that never
		// took hold looks like. So neither label asks its slot to place it. Each takes the whole
		// row and puts its own glyphs at the end it wants, which no slot can get wrong.
		UTextBlock* name = UIStyle::makeLabel(*WidgetTree, FText::FromString(entry.name), UIStyle::bodyFontSize, colour);
		name->SetJustification(ETextJustify::Left);

		UOverlaySlot* nameSlot = row->AddChildToOverlay(name);
		nameSlot->SetHorizontalAlignment(HAlign_Fill);
		nameSlot->SetVerticalAlignment(VAlign_Center);
		nameSlot->SetPadding(FMargin(UIStyle::rowPadding, 0.0f, UIStyle::rowPadding, 0.0f));

		// One word, and the same one whether the server can be joined or not.
		// It used to read Online and a percentage, until that percentage came off the wire.
		// A player picks on whether they can get in, which Full already answers.
		UTextBlock* status = UIStyle::makeLabel(*WidgetTree, serverSelect::statusText(entry.status), UIStyle::bodyFontSize, colour);
		status->SetJustification(ETextJustify::Right);

		UOverlaySlot* statusSlot = row->AddChildToOverlay(status);
		statusSlot->SetHorizontalAlignment(HAlign_Fill);
		statusSlot->SetVerticalAlignment(VAlign_Center);
		statusSlot->SetPadding(FMargin(UIStyle::rowPadding, 0.0f, UIStyle::rowPadding, 0.0f));

		UButton* button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		button->SetStyle(UIStyle::buttonStyle());

		UButtonSlot* contentSlot = Cast<UButtonSlot>(button->AddChild(row));
		if (contentSlot != nullptr)
		{
			contentSlot->SetHorizontalAlignment(HAlign_Fill);
			contentSlot->SetVerticalAlignment(VAlign_Fill);
		}

		if (selectable)
		{
			UServerRowHandler* handler = NewObject<UServerRowHandler>(this);
			handler->owner = this;
			handler->serverId = entry.serverId;
			_handlers.Add(handler);

			button->OnClicked.AddUniqueDynamic(handler, &UServerRowHandler::handleClicked);

			if (_firstRow == nullptr)
			{
				_firstRow = button;
			}
		}
		else
		{
			// Shown and unusable, rather than absent and unexplained.
			button->SetIsEnabled(false);
		}

		UVerticalBoxSlot* rowSlot = _rows->AddChildToVerticalBox(button);
		rowSlot->SetPadding(FMargin(0.0f, UIStyle::rowSpacing, 0.0f, 0.0f));
		rowSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

void UServerSelectWidget::chooseServer(uint8 serverId)
{
	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	if (network == nullptr)
	{
		return;
	}

	// The flow refuses a server the list did not carry.
	// A refusal here means the list moved underneath the screen, and closing on it would hide that.
	if (!network->loginServer().selectServer(serverId))
	{
		return;
	}

	// Remembered so the next login can take this one without asking again.
	// Written only once the flow accepted it, so a refusal never changes what is stored.
	UGameSettings* settings = UGameSettings::get();
	if (settings != nullptr)
	{
		settings->rememberServer(static_cast<int32>(serverId));
	}

	// Closes on the choice, so the character list lands over the login screen.
	// Its Server List button then logs in again, since the login server link ends here.
	_chose = true;
	DeactivateWidget();
}

void UServerSelectWidget::handleBackClicked()
{
	DeactivateWidget();
}

void UServerSelectWidget::NativeOnDeactivated()
{
	// Leaving without choosing abandons the attempt,
	// and the link is dropped rather than left open, since nothing would ever come down it again.
	// Handled here rather than in the Back handler,
	// so an escape and a gamepad back press end exactly as the button does.
	if (!_chose)
	{
		UGameInstance* gameInstance = GetGameInstance();
		UServerConnectionSubsystem* network = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
		if (network != nullptr)
		{
			network->disconnectAll();
		}
	}

	Super::NativeOnDeactivated();
}

UWidget* UServerSelectWidget::NativeGetDesiredFocusTarget() const
{
	// A gamepad lands on the first server it can actually pick, not on Back.
	if (_firstRow != nullptr)
	{
		return _firstRow;
	}

	return _backButton;
}
