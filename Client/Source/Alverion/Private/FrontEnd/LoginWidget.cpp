#include "FrontEnd/LoginWidget.h"

#include "Blueprint/WidgetTree.h"
#include "FrontEnd/CharacterSelectWidget.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Core/GameSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/LocalizedText.h"
#include "UI/OptionsWidget.h"
#include "ServerConnectionSubsystem.h"
#include "FrontEnd/ServerSelectWidget.h"
#include "Sound/SoundBase.h"
#include "UI/UIStyle.h"
#include "UI/UISubsystem.h"
#include "FrontEnd/WorldEntryWidget.h"

namespace
{
	// Loopback, because the only server that answers during development is a local one. -loginserver=host, host:port or :port names another.
	const TCHAR* const defaultHost = TEXT("127.0.0.1");
	constexpr int32 defaultLoginPort = 2106;

	UServerConnectionSubsystem* connectionOf(const UUserWidget& widget)
	{
		UGameInstance* gameInstance = widget.GetGameInstance();
		return (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	}

	UUISubsystem* uiOf(const UUserWidget& widget)
	{
		UGameInstance* gameInstance = widget.GetGameInstance();
		return (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	}

	/** The first server that is up, or the one with this id if it is up. Zero takes any. */
	const Network::FServerListEntry* findOnline(const TArray<Network::FServerListEntry>& servers, int32 wantedId)
	{
		return servers.FindByPredicate([wantedId](const Network::FServerListEntry& entry)
		{
			return (entry.status == Network::EServerStatus::Online) && ((wantedId == 0) || (static_cast<int32>(entry.serverId) == wantedId));
		});
	}

	// Widget name to string table key, matched by name so the tree needs no extra bindings.
	const TMap<FName, FString> loginTextKeys =
	{
		{ TEXT("accountLabel"), TEXT("login.account") },
		{ TEXT("passwordLabel"), TEXT("login.password") },
		{ TEXT("loginLabel"), TEXT("login.login") },
		{ TEXT("optionsLabel"), TEXT("menu.options") },
		{ TEXT("quitLabel"), TEXT("menu.exit") }
	};

	// One faint full screen wash and nothing else, so the world stays visible everywhere.
	// The earlier half screen panel was the point of the 3D front end fighting its own UI.
	constexpr FLinearColor loginWashColor = FLinearColor(0.01f, 0.01f, 0.015f, 0.20f);

	// The column is a fixed width block, centred, rather than anything anchored to an edge.
	// Left anchoring meant the whole screen slid sideways the moment the window was resized.
	constexpr float loginColumnWidth = 520.0f;

	// A touch lighter than the shared label colour, and spaced out the same way the intro's name is.
	constexpr FLinearColor titleColor = FLinearColor(0.95f, 0.93f, 0.86f, 1.0f);
	// The title owns the top of the screen rather than riding on the credential column.
	// At 46 sitting above the fields it read as a caption for them instead of as the game's name.
	constexpr int32 titleFontSize = 92;
	constexpr int32 titleLetterSpacing = 130;
	constexpr float titleTopPad = 90.0f;

	constexpr float loginButtonRadius = 8.0f;
	constexpr int32 loginButtonFontSize = 20;
	constexpr int32 loginButtonLetterSpacing = 40;

	// Options and exit are half width and quiet, because a screen has one primary action.
	constexpr int32 secondaryButtonFontSize = 15;
	constexpr int32 secondaryButtonLetterSpacing = 25;

	// The asset set no size on the credential labels, which lands on the engine default.
	constexpr int32 credentialFontSize = 15;
	constexpr float credentialLabelWidth = 110.0f;
	constexpr FLinearColor credentialLabelRim = FLinearColor(0.4f, 0.4f, 0.42f, 1.0f);

	// Light ink over a dark translucent fill, so the fields sit in the same family as the buttons.
	// The stock light grey boxes read as placeholders against the styled frame around them.
	constexpr FLinearColor textBoxInk = FLinearColor(0.92f, 0.90f, 0.85f, 1.0f);
	constexpr FLinearColor textBoxReadOnlyInk = FLinearColor(0.55f, 0.54f, 0.50f, 1.0f);
	constexpr FLinearColor textBoxFill = FLinearColor(0.02f, 0.03f, 0.04f, 0.62f);
	constexpr FLinearColor textBoxFillFocused = FLinearColor(0.04f, 0.05f, 0.07f, 0.78f);

	// Grey rather than the buttons' brown, because a field is not a thing to press.
	// Without one, a dark translucent fill over a dark scene leaves nothing saying where to type.
	constexpr FLinearColor textBoxOutline = FLinearColor(0.05f, 0.05f, 0.06f, 1.0f);
	constexpr FLinearColor textBoxOutlineHovered = FLinearColor(0.09f, 0.09f, 0.10f, 1.0f);
	constexpr FLinearColor textBoxOutlineFocused = FLinearColor(0.16f, 0.16f, 0.18f, 1.0f);

	constexpr FLinearColor statusColor = FLinearColor(1.0f, 0.85f, 0.5f, 1.0f);
	constexpr int32 statusFontSize = 14;

	// How far the status line sits off the foot of the screen.
	// Clear of the edge without being close enough to the buttons to read as part of them.
	constexpr float statusBottomPad = 60.0f;

	// One of the three wide buttons, which differ only in label name and slot padding.
	UButton* addLoginBarButton(UWidgetTree& tree, UVerticalBox& column, const TCHAR* labelName, const FMargin& padding)
	{
		UButton* button = tree.ConstructWidget<UButton>(UButton::StaticClass());
		button->SetStyle(UIStyle::frontEndButtonStyle(loginButtonRadius));

		UTextBlock* label = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(labelName));
		FSlateFontInfo font = UIStyle::font(loginButtonFontSize);
		font.LetterSpacing = loginButtonLetterSpacing;
		label->SetFont(font);
		label->SetJustification(ETextJustify::Center);
		button->AddChild(label);

		UVerticalBoxSlot* slot = column.AddChildToVerticalBox(button);
		slot->SetPadding(padding);

		return button;
	}

	// One of the two quiet half width buttons sharing the row under the status line.
	UButton* addSecondaryButton(UWidgetTree& tree, UHorizontalBox& row, const TCHAR* labelName, const FMargin& padding)
	{
		UButton* button = tree.ConstructWidget<UButton>(UButton::StaticClass());
		button->SetStyle(UIStyle::frontEndButtonStyle(loginButtonRadius));

		UTextBlock* label = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(labelName));
		FSlateFontInfo font = UIStyle::font(secondaryButtonFontSize);
		font.LetterSpacing = secondaryButtonLetterSpacing;
		label->SetFont(font);
		label->SetJustification(ETextJustify::Center);
		button->AddChild(label);

		UHorizontalBoxSlot* slot = row.AddChildToHorizontalBox(button);
		slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		slot->SetPadding(padding);

		return button;
	}

	// A credential row: right justified label on a fixed column, then the box filling the rest.
	UEditableTextBox* addCredentialRow(UWidgetTree& tree, UVerticalBox& column, const TCHAR* labelName)
	{
		UHorizontalBox* row = tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		UTextBlock* label = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(labelName));

		// A mid grey rim, since these stand on the bright sky rather than on a panel: a light one blurs the letters, a black one is too heavy.
		FSlateFontInfo labelFont = UIStyle::font(credentialFontSize);
		labelFont.OutlineSettings.OutlineSize = 1;
		labelFont.OutlineSettings.OutlineColor = credentialLabelRim;
		label->SetFont(labelFont);
		label->SetColorAndOpacity(FSlateColor(UIStyle::labelColor));
		label->SetMinDesiredWidth(credentialLabelWidth);
		label->SetJustification(ETextJustify::Right);

		UHorizontalBoxSlot* labelSlot = row->AddChildToHorizontalBox(label);
		labelSlot->SetPadding(FMargin(0.0f, 0.0f, 20.0f, 0.0f));
		labelSlot->SetVerticalAlignment(VAlign_Center);

		UEditableTextBox* textBox = tree.ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());

		// Built from the widget's own style so only the inks and fills change.
		FEditableTextBoxStyle style = textBox->GetWidgetStyle();
		style.SetForegroundColor(FSlateColor(textBoxInk));
		style.SetReadOnlyForegroundColor(FSlateColor(textBoxReadOnlyInk));
		style.SetFocusedForegroundColor(FSlateColor(textBoxInk));
		// Drawn as rounded boxes rather than tinted, because the stock brush is an image.
		// An image has no outline to give.
		// The helper is the buttons' own, so the fields share their corner radius.
		style.BackgroundImageNormal = UIStyle::stateBrush(textBoxFill, textBoxOutline, loginButtonRadius);
		style.BackgroundImageHovered = UIStyle::stateBrush(textBoxFill, textBoxOutlineHovered, loginButtonRadius);
		style.BackgroundImageFocused = UIStyle::stateBrush(textBoxFillFocused, textBoxOutlineFocused, loginButtonRadius);
		style.BackgroundImageReadOnly = UIStyle::stateBrush(textBoxFill, textBoxOutline, loginButtonRadius);
		textBox->SetWidgetStyle(style);

		UHorizontalBoxSlot* boxSlot = row->AddChildToHorizontalBox(textBox);
		boxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		boxSlot->SetVerticalAlignment(VAlign_Center);

		UVerticalBoxSlot* rowSlot = column.AddChildToVerticalBox(row);
		rowSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 6.0f));

		return textBox;
	}
}

TSharedRef<SWidget> ULoginWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		// The title sits at the top of the screen, the credential column is centred under it.
		UOverlay* root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		UImage* wash = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush washBrush;
		washBrush.TintColor = FSlateColor(loginWashColor);
		wash->SetBrush(washBrush);

		UOverlaySlot* washSlot = root->AddChildToOverlay(wash);
		washSlot->SetHorizontalAlignment(HAlign_Fill);
		washSlot->SetVerticalAlignment(VAlign_Fill);

		UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		USizeBox* columnBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		columnBox->SetWidthOverride(loginColumnWidth);
		columnBox->AddChild(column);

		UOverlaySlot* columnSlot = root->AddChildToOverlay(columnBox);
		columnSlot->SetHorizontalAlignment(HAlign_Center);
		columnSlot->SetVerticalAlignment(VAlign_Center);

		// The game's name is a literal, not a table entry. It reads the same in every language.
		UTextBlock* title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		title->SetText(FText::FromString(TEXT("Alverion Online")));
		title->SetColorAndOpacity(FSlateColor(titleColor));
		title->SetJustification(ETextJustify::Center);

		FSlateFontInfo titleFont = UIStyle::font(titleFontSize);
		titleFont.LetterSpacing = titleLetterSpacing;
		title->SetFont(titleFont);

		UOverlaySlot* titleSlot = root->AddChildToOverlay(title);
		titleSlot->SetHorizontalAlignment(HAlign_Center);
		titleSlot->SetVerticalAlignment(VAlign_Top);
		titleSlot->SetPadding(FMargin(0.0f, titleTopPad, 0.0f, 0.0f));

		accountTextBox = addCredentialRow(*WidgetTree, *column, TEXT("accountLabel"));
		passwordTextBox = addCredentialRow(*WidgetTree, *column, TEXT("passwordLabel"));

		loginButton = addLoginBarButton(*WidgetTree, *column, TEXT("loginLabel"), FMargin(0.0f, 18.0f, 0.0f, 6.0f));

		statusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		statusText->SetColorAndOpacity(FSlateColor(statusColor));
		statusText->SetFont(UIStyle::font(statusFontSize));
		statusText->SetJustification(ETextJustify::Center);

		// The foot of the screen rather than inside the credential column.
		// Sitting between Login and the row under it, this line moved both buttons down whenever
		// it had something to say.
		// The screen shifted under the cursor exactly when somebody was reading it.
		// Down here it appears and disappears without touching anything else.
		UOverlaySlot* statusSlot = root->AddChildToOverlay(statusText);
		statusSlot->SetHorizontalAlignment(HAlign_Center);
		statusSlot->SetVerticalAlignment(VAlign_Bottom);
		statusSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, statusBottomPad));

		UHorizontalBox* secondaryRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		optionsButton = addSecondaryButton(*WidgetTree, *secondaryRow, TEXT("optionsLabel"), FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		quitButton = addSecondaryButton(*WidgetTree, *secondaryRow, TEXT("quitLabel"), FMargin(8.0f, 0.0f, 0.0f, 0.0f));

		UVerticalBoxSlot* secondarySlot = column->AddChildToVerticalBox(secondaryRow);
		secondarySlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));

		WidgetTree->RootWidget = root;

		// The class default only lived in the asset, so it is loaded here rather than lost with the blueprint.
		if (clickSound == nullptr)
		{
			clickSound = LoadObject<USoundBase>(nullptr, UIStyle::clickSoundPath);

			if (clickSound == nullptr)
			{
				UE_LOG(LogTemp, Warning, TEXT("LoginWidget could not load %s, so its buttons are silent."), UIStyle::clickSoundPath);
			}
		}
	}

	return Super::RebuildWidget();
}

void ULoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (loginButton != nullptr)
	{
		loginButton->OnClicked.AddUniqueDynamic(this, &ULoginWidget::handleLoginClicked);
	}

	if (optionsButton != nullptr)
	{
		optionsButton->OnClicked.AddUniqueDynamic(this, &ULoginWidget::handleOptionsClicked);
	}

	if (quitButton != nullptr)
	{
		quitButton->OnClicked.AddUniqueDynamic(this, &ULoginWidget::handleQuitClicked);
	}

	// Enter in either field submits, which is what every login screen has taught players to expect.
	if (accountTextBox != nullptr)
	{
		accountTextBox->OnTextCommitted.AddUniqueDynamic(this, &ULoginWidget::handleCredentialCommitted);
	}

	if (passwordTextBox != nullptr)
	{
		passwordTextBox->SetIsPassword(true);
		passwordTextBox->OnTextCommitted.AddUniqueDynamic(this, &ULoginWidget::handleCredentialCommitted);
	}

	if (statusText != nullptr)
	{
		statusText->SetText(FText::GetEmpty());
	}

	LocalizedText::applyToTree(WidgetTree, loginTextKeys);

	// Back from the world with the character list already held: straight to it, without the login form or a second automatic login.
	// Pushed once this screen is active rather than now, since it is still being added to the same stack and would land over the list.
	UServerConnectionSubsystem* network = connectionOf(*this);
	if ((network != nullptr) && (network->gameServer().step() == Network::EGameServerStep::CharacterSelect))
	{
		_backFromWorld = true;
		return;
	}

	applyCommandLineCredentials();
}
void ULoginWidget::NativeDestruct()
{
	// The flows belong to the subsystem and survive this screen, so bindings left behind would aim at a destroyed widget.
	UServerConnectionSubsystem* network = connectionOf(*this);
	if (network != nullptr)
	{
		network->loginServer().onStepChanged.Unbind();
		network->loginServer().onFailed.Unbind();
		network->gameServer().onStepChanged.Unbind();
		network->gameServer().onFailed.Unbind();
	}

	// The password lives no longer than the screen that took it.
	_account.Empty();
	_password.Empty();

	Super::NativeDestruct();
}

void ULoginWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	if (_backFromWorld)
	{
		_backFromWorld = false;
		bindGameServer();
		showCharacterSelect();
		return;
	}

	// NativeGetDesiredFocusTarget names this field, but that leaves the caret outside it. Asked for again here, where the widget is on screen.
	if (accountTextBox != nullptr)
	{
		accountTextBox->SetKeyboardFocus();
	}

	// Uncovered by the character screen's Server List button: the login server link closed when a server was chosen, so the list needs a fresh login.
	UUISubsystem* ui = uiOf(*this);
	if ((ui != nullptr) && ui->forceServerList && !_account.IsEmpty())
	{
		beginLogin(_account, _password);
	}
}

UWidget* ULoginWidget::NativeGetDesiredFocusTarget() const
{
	// Typing should start in the account field rather than on a button.
	if (accountTextBox != nullptr)
	{
		return accountTextBox;
	}

	return Super::NativeGetDesiredFocusTarget();
}

void ULoginWidget::applyCommandLineCredentials()
{
#if !UE_BUILD_SHIPPING
	// -autologin=account,password[,character] for testing without a hand on the keyboard. Shipping builds have no such door.
	FString autoLogin;
	TArray<FString> parts;
	if (!FParse::Value(FCommandLine::Get(), TEXT("autologin="), autoLogin, false) || (autoLogin.ParseIntoArray(parts, TEXT(",")) < 2))
	{
		return;
	}

	if (accountTextBox != nullptr)
	{
		accountTextBox->SetText(FText::FromString(parts[0]));
	}

	if (passwordTextBox != nullptr)
	{
		passwordTextBox->SetText(FText::FromString(parts[1]));
	}

	_autoLogin = true;
	_autoEnter = parts.IsValidIndex(2) ? parts[2] : FString();
	submitLogin();
#endif
}

void ULoginWidget::submitLogin()
{
	const FString account = (accountTextBox != nullptr) ? accountTextBox->GetText().ToString() : FString();
	if (account.IsEmpty())
	{
		setStatus(LocalizedText::get(TEXT("login.account_required")));
		return;
	}

	const FString password = (passwordTextBox != nullptr) ? passwordTextBox->GetText().ToString() : FString();
	setStatus(LocalizedText::get(TEXT("login.connecting")));
	setBusy(true);
	beginLogin(account, password);
}

void ULoginWidget::beginLogin(const FString& account, const FString& password)
{
	UServerConnectionSubsystem* network = connectionOf(*this);
	if (network == nullptr)
	{
		setStatus(LocalizedText::get(TEXT("login.could_not_connect")));
		setBusy(false);
		return;
	}

	// A fresh attempt, so the character list may be shown again.
	_characterSelectPushed = false;

	FString host = defaultHost;
	int32 port = defaultLoginPort;
	FString target;
	if (FParse::Value(FCommandLine::Get(), TEXT("loginserver="), target) && !target.IsEmpty())
	{
		FString hostText;
		FString portText;
		if (!target.Split(TEXT(":"), &hostText, &portText))
		{
			hostText = target;
		}

		// host, host:port or :port. Whatever part is left out, or a port out of range, keeps its default.
		if (!hostText.IsEmpty())
		{
			host = hostText;
		}

		const int32 parsedPort = FCString::Atoi(*portText);
		if ((parsedPort > 0) && (parsedPort <= 65535))
		{
			port = parsedPort;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("Logging in through %s on port %d."), *host, port);

	// Weak, because the flow lives on the subsystem and outlives this screen.
	network->loginServer().onStepChanged.BindWeakLambda(this, [this](Network::ELoginServerStep step)
	{
		if (step == Network::ELoginServerStep::ServerSelect)
		{
			showServerSelect();
		}
		else if (step == Network::ELoginServerStep::TokenIssued)
		{
			enterGameServer();
		}
	});

	network->loginServer().onFailed.BindWeakLambda(this, [this](const FString& reason)
	{
		// The reason goes to the log rather than the screen: it names the socket or packet that broke, which is no help to a player.
		UE_LOG(LogTemp, Display, TEXT("Login failed: %s"), *reason);
		setStatus(LocalizedText::get(TEXT("login.could_not_connect")));
		setBusy(false);
	});

	if (!network->startLogin(host, port, account, password))
	{
		UE_LOG(LogTemp, Display, TEXT("Login failed: no answer from %s on port %d."), *host, port);
		setStatus(LocalizedText::get(TEXT("login.could_not_connect")));
		setBusy(false);
		return;
	}

	// Held for this session so the server list can be asked for again from the character screen, which costs a fresh login.
	// That keeps the password in memory, which is why both are cleared the moment this screen goes away.
	_account = account;
	_password = password;
}

void ULoginWidget::showServerSelect()
{
	UServerConnectionSubsystem* network = connectionOf(*this);
	UUISubsystem* ui = uiOf(*this);
	if ((network == nullptr) || (ui == nullptr))
	{
		setStatus(LocalizedText::get(TEXT("login.could_not_connect")));
		setBusy(false);
		return;
	}

	// One trip only, and cleared whether it is honoured or not, so a stale flag never strands somebody on the list.
	const bool asked = ui->forceServerList;
	ui->forceServerList = false;

	// A server chosen before is taken again without asking, which makes the second launch one click rather than two.
	// Only while the list still says it is up: a server gone down overnight is when the player wants the list.
	// An automatic login takes the first one up, since nobody is there to choose.
	const UGameSettings* settings = UGameSettings::get();
	const int32 remembered = (settings != nullptr) ? settings->getRememberedServer() : 0;
	const Network::FServerListEntry* pick = nullptr;
	if (_autoLogin)
	{
		pick = findOnline(network->loginServer().servers(), 0);
	}
	else if (!asked && (remembered != 0))
	{
		pick = findOnline(network->loginServer().servers(), remembered);
	}

	if ((pick != nullptr) && network->loginServer().selectServer(pick->serverId))
	{
		return;
	}

	// The Menu layer, so login stays underneath and comes back when the choice screen closes.
	ui->pushWidget(EUILayer::Menu, UServerSelectWidget::StaticClass());
	setBusy(false);
}

void ULoginWidget::enterGameServer()
{
	UServerConnectionSubsystem* network = connectionOf(*this);
	if (network == nullptr)
	{
		return;
	}

	bindGameServer();
	if (!network->startGameServer())
	{
		setStatus(LocalizedText::get(TEXT("login.could_not_connect")));
		setBusy(false);
	}
}

void ULoginWidget::bindGameServer()
{
	UServerConnectionSubsystem* network = connectionOf(*this);
	if (network == nullptr)
	{
		return;
	}

	network->gameServer().onStepChanged.BindWeakLambda(this, [this](Network::EGameServerStep step)
	{
		if (step == Network::EGameServerStep::CharacterSelect)
		{
			showCharacterSelect();
		}
		else if (step == Network::EGameServerStep::Selected)
		{
			showWorldEntry();
		}
	});

	network->gameServer().onFailed.BindWeakLambda(this, [this](const FString& reason)
	{
		UE_LOG(LogTemp, Display, TEXT("Login failed: %s"), *reason);

		// A lost link takes the character screen with it, so the login screen and its status line are what is left.
		if (_characterSelect.IsValid())
		{
			_characterSelect->DeactivateWidget();
		}

		_characterSelectPushed = false;
		setStatus(LocalizedText::get(TEXT("login.could_not_connect")));
		setBusy(false);
	});
}

void ULoginWidget::showCharacterSelect()
{
	UServerConnectionSubsystem* network = connectionOf(*this);

#if !UE_BUILD_SHIPPING
	// The character named on the command line is entered as soon as the list holds it.
	if ((network != nullptr) && !_autoEnter.IsEmpty())
	{
		const FString wantedName = _autoEnter;
		_autoEnter.Reset();

		const Network::FCharacterListEntry* wanted = network->gameServer().characters().FindByPredicate([&wantedName](const Network::FCharacterListEntry& entry)
		{
			return entry.name == wantedName;
		});

		if ((wanted != nullptr) && network->gameServer().selectCharacter(wanted->objectId))
		{
			return;
		}
	}
#endif

	// The list comes again after every creation and deletion. The screen already up only has to catch up.
	if (_characterSelectPushed)
	{
		if (_characterSelect.IsValid())
		{
			_characterSelect->refresh();
		}

		return;
	}

	UUISubsystem* ui = uiOf(*this);
	if (ui == nullptr)
	{
		setBusy(false);
		return;
	}

	// The same layer as the server list, so backing out of one uncovers whatever it was pushed over.
	_characterSelect = Cast<UCharacterSelectWidget>(ui->pushWidget(EUILayer::Menu, UCharacterSelectWidget::StaticClass()));
	_characterSelectPushed = true;
	setBusy(false);
}

void ULoginWidget::showWorldEntry()
{
	// The Menu layer again, so it covers the character list. Nothing pops it: the level travel it starts takes the whole front end away.
	UUISubsystem* ui = uiOf(*this);
	if (ui != nullptr)
	{
		ui->pushWidget(EUILayer::Menu, UWorldEntryWidget::StaticClass());
	}
}

void ULoginWidget::setStatus(const FText& text)
{
	// Logged as well as shown.
	// A refusal during startup is a developer's problem and the status line may be gone by then.
	UE_LOG(LogTemp, Display, TEXT("LoginWidget status: %s"), *text.ToString());

	if (statusText != nullptr)
	{
		statusText->SetText(text);
	}
}

void ULoginWidget::setBusy(bool isBusy)
{
	if (loginButton != nullptr)
	{
		loginButton->SetIsEnabled(!isBusy);
	}
}

void ULoginWidget::playClickSound() const
{
	if (clickSound == nullptr)
	{
		return;
	}

	const UGameSettings* settings = UGameSettings::get();
	const float volume = (settings != nullptr) ? settings->getEffectiveSfxVolume() : 1.0f;

	UGameplayStatics::PlaySound2D(this, clickSound, volume);
}

void ULoginWidget::handleLoginClicked()
{
	playClickSound();

	submitLogin();
}

void ULoginWidget::handleOptionsClicked()
{
	playClickSound();

	UGameInstance* gameInstance = GetGameInstance();
	if (gameInstance == nullptr)
	{
		return;
	}

	UUISubsystem* subsystem = gameInstance->GetSubsystem<UUISubsystem>();
	if (subsystem == nullptr)
	{
		return;
	}

	// Options goes onto the same layer, so login stays underneath and returns when it deactivates.
	// The C++ class itself, because the options screen has no widget blueprint any more.
	subsystem->pushWidget(EUILayer::Menu, UOptionsWidget::StaticClass());
}

void ULoginWidget::handleQuitClicked()
{
	playClickSound();

	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	if (subsystem == nullptr)
	{
		UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
		return;
	}

	// Weak, because the answer arrives after the dialog closes and this screen may be gone by then.
	subsystem->askConfirm(LocalizedText::get(TEXT("menu.exit_confirm")), FOnConfirmAnswered::CreateWeakLambda(this, [this](bool confirmed)
	{
		if (confirmed)
		{
			UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
		}
	}));
}

void ULoginWidget::handleCredentialCommitted(const FText& text, ETextCommit::Type commitMethod)
{
	if (commitMethod == ETextCommit::OnEnter)
	{
		submitLogin();
	}
}
