#pragma once

#include "CommonActivatableWidget.h"
#include "CoreMinimal.h"

#include "LoginWidget.generated.h"

class UButton;
class UCharacterSelectWidget;
class UEditableTextBox;
class USoundBase;
class UTextBlock;

// The first screen after the intro: account name, password and Login, with Options and Exit below.
// Login drives the login server and then the game server through the connection subsystem's flows, and pushes each screen as its step arrives.
UCLASS()
class ALVERION_API ULoginWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

	// Releases the flow delegates, which outlive this widget because the session does.
	virtual void NativeDestruct() override;

protected:
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

	// Puts the caret in the account field rather than merely handing the field focus.
	virtual void NativeOnActivated() override;

	// Played on every button press at master times effects.
	UPROPERTY(EditDefaultsOnly, Category = "Login")
	TObjectPtr<USoundBase> clickSound;

	// Name the fields and buttons like this in the widget blueprint.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> accountTextBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> passwordTextBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> loginButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> optionsButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> quitButton;

	// Carries the server's reason for a refusal, or what the client is waiting on.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> statusText;

private:
	void playClickSound() const;

	/**
	 * Reads -autologin=account,password[,character], development builds only, because a password on a command line is visible in the process list.
	 * The server address, -loginserver=host[:port] or :port, is read by beginLogin in every build.
	 */
	void applyCommandLineCredentials();

	void submitLogin();

	// Connects to the login server with the credentials. The steps that follow arrive through the login server flow.
	void beginLogin(const FString& account, const FString& password);

	// Takes a remembered server again, or puts the server list in front of the player.
	void showServerSelect();

	// Puts the character list in front of the player, or refreshes the one already up when the server sends the list again.
	void showCharacterSelect();

	// Covers the trip into the world once a character has been granted.
	void showWorldEntry();

	// Takes the token the login server issued to the game server it named.
	void enterGameServer();

	// Listens to the game server link for the character list, the world entry and a lost connection.
	void bindGameServer();

	void setStatus(const FText& text);

	// A second press cannot open a second session while one is in flight.
	void setBusy(bool isBusy);

	UFUNCTION()
	void handleLoginClicked();

	UFUNCTION()
	void handleOptionsClicked();

	UFUNCTION()
	void handleQuitClicked();

	UFUNCTION()
	void handleCredentialCommitted(const FText& text, ETextCommit::Type commitMethod);

	// The step returns to CharacterSelect after a creation as well as after the first list,
	// and pushing that screen again on the way back would stack a second copy of it.
	bool _characterSelectPushed = false;

	// Constructed on the way back from the world, with the list already held: the list goes up as soon as this screen is active.
	bool _backFromWorld = false;

	// The character screen, so a list sent again after a creation or deletion refreshes it rather than stacking a second one.
	TWeakObjectPtr<UCharacterSelectWidget> _characterSelect;

	// Set by -autologin: the first server up is taken, and _autoEnter names the character entered once the list arrives.
	bool _autoLogin = false;
	FString _autoEnter;

	// What the last login used, kept so the server list can be asked for again: the login server closes its link once a server is chosen,
	// so a second pick means a fresh login. The alternative was asking the player to type it again.
	// Cleared in NativeDestruct: the password lives no longer than this screen.
	FString _account;
	FString _password;
};
