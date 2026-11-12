#pragma once

#include "CoreMinimal.h"
#include "InputAction.h"
#include "Subsystems/LocalPlayerSubsystem.h"

#include "InputBootstrapSubsystem.generated.h"

class UInputMappingContext;

// An action made at runtime that carries its own rebindable name, which Enhanced Input lets only a subclass set.
UCLASS()
class ALVERION_API UMappableInputAction : public UInputAction
{
	GENERATED_BODY()

public:
	void makeMappable(FName name, const FText& displayName);
};

// Builds the default mapping context and hands it to Enhanced Input, both to play with and to rebind against.
// The defaults live in C++ rather than in the asset because the editor tooling cannot write the mapping array,
// and a table here sits next to the action list it mirrors.
UCLASS()
class ALVERION_API UInputBootstrapSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& collection) override;

	UFUNCTION(BlueprintPure, Category = "Input")
	UInputMappingContext* getDefaultMappingContext() const;

	// The mappable names in table order, which is the order the options screen lists them in.
	// Enhanced Input keeps its rows in a map, so without this the screen orders itself by hash and nobody can learn where anything is.
	UFUNCTION(BlueprintPure, Category = "Input")
	TArray<FName> getDefaultMappingOrder() const;

	// Puts the default context on whatever player input exists right now.
	//
	// Enhanced Input keeps applied contexts on the player controller's player input rather than on the local player, and travel destroys the controller.
	// So adding this once at startup covered the front end and left the world with no bindings at all: every action dead, and only raw key binds still answering.
	// Called again from the controller whenever one sets its input up, which is once per level.
	UFUNCTION(BlueprintCallable, Category = "Input")
	void applyDefaultMappings();

	// Safe to call again. Enhanced Input builds its key profile after local player subsystems initialize, so a
	// registration made during Initialize can land before there is a profile to hold it.
	UFUNCTION(BlueprintCallable, Category = "Input")
	void registerWithUserSettings();

	// The action that uses an action bar slot, or null past the bar's end.
	UInputAction* getShortcutAction(int32 index) const;

private:
	void buildDefaultMappings();

	UPROPERTY()
	TObjectPtr<UInputMappingContext> _defaultMappingContext;

	// One per action bar slot, made at startup: each needs a rebindable name of its own, and ten assets would only repeat each other.
	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> _shortcutActions;
};
