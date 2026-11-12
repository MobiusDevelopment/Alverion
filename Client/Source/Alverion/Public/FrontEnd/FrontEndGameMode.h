#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UI/UISubsystem.h"

#include "FrontEndGameMode.generated.h"

class ACameraActor;
class UCommonActivatableWidget;
class UIntroWidget;
class URootLayout;

namespace Network
{
	struct FAppearance;
}

/**
 * The front end on L_Login: the root layout and its screens, the hand-placed camera stops they move between,
 * the figure standing on a preview spot, and the menu theme. Hands over to the world when a character is chosen.
 */
UCLASS()
class ALVERION_API AFrontEndGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFrontEndGameMode();

	/** Where the world is loaded, with the world's game mode named in the travel options. */
	static void travelToWorld(UObject* worldContext);

	virtual void BeginPlay() override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FCharacterSelectionIdleTest;
	friend class FOrcPlayerCreationTest;
#endif

	UPROPERTY()
	TObjectPtr<URootLayout> _rootLayout;

	UPROPERTY()
	TObjectPtr<UIntroWidget> _introWidget;

	// The camera actors placed in the level, by the tag that says which screen each one frames.
	UPROPERTY()
	TMap<FName, TObjectPtr<ACameraActor>> _cameraStops;

	// The marker actors an avatar can stand on, read from the level once like the camera stops.
	UPROPERTY()
	TMap<EAvatarSpot, TObjectPtr<AActor>> _avatarSpots;

	// Whoever is standing on a spot right now. One at a time, since only one screen shows a body.
	UPROPERTY()
	TObjectPtr<AActor> _avatar;

	EAvatarSpot _avatarSpot = EAvatarSpot::Select;

	void collectCameraStops();
	void focusStop(const FName& stopTag, bool instant);
	void watchMenuLayer();
	void handleMenuWidgetChanged(UCommonActivatableWidget* widget);

	void collectAvatarSpots();
	void installAvatarHooks();
	void showAvatar(EAvatarSpot spot, TConstArrayView<uint8> appearance, TConstArrayView<int32> worn);
	/** Worn is the garments shown, the whole starter outfit unless said otherwise. The creation spot always stands in underwear. */
	void standAvatar(EAvatarSpot spot, UClass* bodyClass, const Network::FAppearance& appearance, uint8 worn = 0x3F);
	void clearAvatar();

	// Counts avatar requests, so a body that arrives after the player has moved on is dropped rather than shown.
	uint32 _avatarRequest = 0;
	void turnAvatar(float degrees);
	void zoomView(float degrees);
	FRotator aimAtHead(const ACameraActor& camera) const;

	// What the live stop looked like before a zoom narrowed it, so leaving the screen puts it back. Zero means nothing is zoomed.
	float _restingFieldOfView = 0.0f;
	FRotator _restingRotation = FRotator::ZeroRotator;

	void createRootLayout();
	void createIntro();
	void showLogin();

	UFUNCTION()
	void handleIntroFinished();
};
