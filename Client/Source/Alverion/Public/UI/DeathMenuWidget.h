#pragma once

#include "CommonActivatableWidget.h"
#include "CoreMinimal.h"

#include "DeathMenuWidget.generated.h"

class UButton;

/**
 * Over the world once the character has died: "You have died" and a Respawn button, which is the only way out of it.
 * Escape and Back leave it up, since a fallen character has nothing else to do.
 */
UCLASS()
class ALVERION_API UDeathMenuWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** What Respawn does, once the menu has closed. */
	void setup(TFunction<void()> onRespawn);

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

protected:
	virtual bool NativeOnHandleBackAction() override;

private:
	UFUNCTION()
	void handleRespawnClicked();

	UPROPERTY()
	TObjectPtr<UButton> _respawnButton;

	TFunction<void()> _onRespawn;
};
