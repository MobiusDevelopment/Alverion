#pragma once

#include "Blueprint/UserWidget.h"
#include "CommonActivatableWidget.h"
#include "CoreMinimal.h"
#include "GameServer/GameServerProtocol.h"

#include "LootWidget.generated.h"

class UButton;
class ULootWidget;
class UVerticalBox;
class UTextBlock;

/** One stack in the loot window: its picture, name and count, taken by clicking it. */
UCLASS()
class ALVERION_API ULootRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void setup(ULootWidget* owner, int32 index, const Network::FLootStack& stack);
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void handleClicked();

	UPROPERTY()
	TObjectPtr<ULootWidget> _owner;

	int32 _index = 0;
	Network::FLootStack _stack;
};

/**
 * What a corpse holds, over the world once the interact key opens it: a row a stack, each taken by clicking it, and Take All.
 * Every answer from the server replaces the list, and an empty one, nothing left or the corpse gone, closes the window.
 */
UCLASS()
class ALVERION_API ULootWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Shows what the corpse holds now, under its name. */
	void show(const Network::FCorpseLoot& loot, const FText& corpseName);

	/** The corpse the window shows, zero before the first list. */
	uint32 corpseId() const;

	/** Asks the server for one stack, named by its place in the list on show. */
	void takeStack(int32 index, int32 itemId);

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

private:
	UFUNCTION()
	void handleTakeAllClicked();

	/** Lays the rows out again from the list on show. */
	void refreshRows();

	UPROPERTY()
	TObjectPtr<UTextBlock> _title;

	UPROPERTY()
	TObjectPtr<UVerticalBox> _rows;

	UPROPERTY()
	TObjectPtr<UButton> _takeAllButton;

	Network::FCorpseLoot _loot;
	FText _corpseName;
};
