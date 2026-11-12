#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameServer/GameServerPackets.h"

#include "WorldCharacter.generated.h"

class APlayerController;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class USpringArmComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class UWorldPartitionStreamingSourceComponent;
struct FInputActionValue;

/** The player's own character. It moves itself and tells the server where it is, which the server accepts in keyboard mode. */
UCLASS()
class ALVERION_API AWorldCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AWorldCharacter();

	virtual void Tick(float deltaSeconds) override;
	virtual void PawnClientRestart() override;
	virtual void SetupPlayerInputComponent(UInputComponent* input) override;

	/** Stands the MetaHuman this appearance describes on the character, in place of the placeholder figure. */
	void wear(const Network::FAppearance& appearance);

	/** Films the character from the front for the character sheet, every frame until endPortrait, and returns the picture. */
	UTextureRenderTarget2D* beginPortrait();
	void endPortrait();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type reason) override;

private:
	void updateViewHeight();

	void moveForward();
	void moveBackward();
	void moveLeft();
	void moveRight();
	void move(float forward, float right);
	void rise();
	void toggleAutoRun();
	void toggleRearView();
	void toggleFirstPerson();

	/** Whether a key, button or click other than the auto run key or the wheel went down this frame. */
	bool pressedOtherThanAutoRun(const APlayerController& controller) const;

	void look(const FInputActionValue& value);
	void zoom(const FInputActionValue& value);
	void corrected(const Network::FObjectPlace& place, bool teleport);

	/** Puts the character on the ground under it and lets it walk, once streamed ground is there. */
	bool settleOnGround();

	/** Swims where the water is too deep to stand in, floating at the water line, and stands again where it shallows. */
	void swimOrStand(float deltaSeconds);

	UPROPERTY()
	TObjectPtr<USpringArmComponent> _arm;

	UPROPERTY()
	TObjectPtr<UCameraComponent> _camera;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> _body;

	/** The worn MetaHuman, riding on the capsule. Destroyed with the character, since an attached actor outlives its parent. */
	UPROPERTY()
	TObjectPtr<AActor> _metaHuman;

	/** The continent streams around the player, and loads nothing without a source. */
	UPROPERTY()
	TObjectPtr<UWorldPartitionStreamingSourceComponent> _streaming;

	/** Made on the first character sheet and kept, filming only while a sheet is open. */
	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> _portraitCapture;

	/** The underwater haze on the camera, told the camera's height above the sea every frame. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> _underwater;

	/** Mouse look and wheel zoom only. The movement keys live in the rebindable context the input bootstrap builds from the action assets. */
	UPROPERTY()
	TObjectPtr<UInputMappingContext> _lookMapping;

	UPROPERTY()
	TObjectPtr<UInputAction> _lookAction;

	UPROPERTY()
	TObjectPtr<UInputAction> _zoomAction;

	/** Where the wheel asked the camera to be. Tick glides the arm there, so a notch is not a jump. */
	float _cameraDistanceCm = 0.0f;

	UPROPERTY()
	TObjectPtr<UInputAction> _moveForwardAction;

	UPROPERTY()
	TObjectPtr<UInputAction> _moveBackwardAction;

	UPROPERTY()
	TObjectPtr<UInputAction> _moveLeftAction;

	UPROPERTY()
	TObjectPtr<UInputAction> _moveRightAction;

	UPROPERTY()
	TObjectPtr<UInputAction> _jumpAction;

	UPROPERTY()
	TObjectPtr<UInputAction> _autoRunAction;

	/** Set by the auto run key and cleared by it or by any other press: while set the character runs, or swims, where the camera looks. */
	bool _autoRun = false;

	UPROPERTY()
	TObjectPtr<UInputAction> _backViewAction;

	/** Set and cleared by the rear view key: the camera looks behind, and movement keeps the way it faced before. */
	bool _rearView = false;

	UPROPERTY()
	TObjectPtr<UInputAction> _switchCameraAction;

	/** Set and cleared by the switch camera key: the camera sits at the eyes, the worn body is hidden and turns with the camera. */
	bool _firstPerson = false;

	/** Set by any movement key this frame, read and cleared by Tick: four keys can be held at once, so no single release means stopped. */
	bool _movedThisFrame = false;
	bool _onGround = false;
	bool _swimming = false;

	/** Set on wading in, while the swimmer floats up to the water line, and cleared there or once it dives. */
	bool _surfacing = false;
};
