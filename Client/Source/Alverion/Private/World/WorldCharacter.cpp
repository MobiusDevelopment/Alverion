#include "World/WorldCharacter.h"

#include "Appearance/AppearanceApplier.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Core/GameSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "World/SceneSetup.h"
#include "World/Water.h"
#include "ServerConnectionSubsystem.h"

namespace
{
	// A starting character's 115 units a second at the client's scale. The server takes this client's word for where it is, so the two need not match exactly.
	constexpr float runSpeedCm = 460.0f;

	constexpr float capsuleRadiusCm = 34.0f;
	constexpr float capsuleHalfHeightCm = 90.0f;
	constexpr float cameraDistanceCm = 450.0f;

	// Where the camera arm starts above the capsule's middle: over the shoulders from behind, at the eyes in first person.
	constexpr float thirdPersonArmHeightCm = 60.0f;
	constexpr float firstPersonArmHeightCm = 75.0f;

	// The character sheet's picture is upright; its camera stands in front of the body far enough off to take it head to foot.
	constexpr int32 portraitWidth = 400;
	constexpr int32 portraitHeight = 560;
	constexpr float portraitDistanceCm = 260.0f;
	constexpr float portraitFieldOfView = 35.0f;

	// The mouse wheel's reach, from over the shoulder to a wide view, how far one notch moves the camera and how quickly it glides there.
	constexpr float cameraNearestCm = 150.0f;
	constexpr float cameraFarthestCm = 1500.0f;
	constexpr float cameraZoomStepCm = 75.0f;
	constexpr float cameraZoomRate = 10.0f;

	// How far above and below the server's height to look for ground, which is shaped by hand and need not match it.
	constexpr double settleSearchCm = 20000.0;

	const FLinearColor ownBodyColor(0.2f, 0.45f, 0.9f);

	// The speed in centimetres a second below which the body stands still rather than runs or swims.
	constexpr double standingSpeedCm = 10.0;

	// A starting character's 50 swim units a second at the client's scale, and how hard the water stops a swimmer who lets go of the keys.
	constexpr float swimSpeedCm = 200.0f;
	constexpr float swimBrakingCm = 1200.0f;

	// How quickly a character wading in floats up to the water line.
	constexpr float floatRate = 4.0f;

	// What the options screen's sensitivity reads on a fresh install, which is the turn rate the mouse had before the slider existed.
	constexpr float defaultMouseSensitivity = 0.3f;

	const TCHAR* const moveForwardPath = TEXT("/Game/Input/Actions/IA_MoveForward.IA_MoveForward");
	const TCHAR* const moveBackwardPath = TEXT("/Game/Input/Actions/IA_MoveBackward.IA_MoveBackward");
	const TCHAR* const moveLeftPath = TEXT("/Game/Input/Actions/IA_MoveLeft.IA_MoveLeft");
	const TCHAR* const moveRightPath = TEXT("/Game/Input/Actions/IA_MoveRight.IA_MoveRight");
	const TCHAR* const jumpPath = TEXT("/Game/Input/Actions/IA_Jump.IA_Jump");
	const TCHAR* const autoRunPath = TEXT("/Game/Input/Actions/IA_AutoRun.IA_AutoRun");
	const TCHAR* const backViewPath = TEXT("/Game/Input/Actions/IA_BackView.IA_BackView");
	const TCHAR* const switchCameraPath = TEXT("/Game/Input/Actions/IA_SwitchCamera.IA_SwitchCamera");

	UServerConnectionSubsystem* connectionOf(const AActor& actor)
	{
		UGameInstance* instance = actor.GetGameInstance();
		return (instance != nullptr) ? instance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	}
}

AWorldCharacter::AWorldCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(capsuleRadiusCm, capsuleHalfHeightCm);

	// The body faces where it walks and the camera turns freely around it.
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = runSpeedCm;

	// A swimmer flies level at the water line, so the flying limits are the water's.
	GetCharacterMovement()->MaxFlySpeed = swimSpeedCm;
	GetCharacterMovement()->BrakingDecelerationFlying = swimBrakingCm;

	_arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
	_arm->SetupAttachment(RootComponent);
	_arm->TargetArmLength = cameraDistanceCm;
	_cameraDistanceCm = cameraDistanceCm;
	_arm->SetRelativeLocation(FVector(0.0f, 0.0f, thirdPersonArmHeightCm));
	_arm->bUsePawnControlRotation = true;

	_camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	_camera->SetupAttachment(_arm, USpringArmComponent::SocketName);

	_body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	_body->SetupAttachment(RootComponent);

	_streaming = CreateDefaultSubobject<UWorldPartitionStreamingSourceComponent>(TEXT("Streaming"));
}

void AWorldCharacter::BeginPlay()
{
	Super::BeginPlay();

	SceneSetup::paintPlaceholderBody(*_body, ownBodyColor);

	// Underwater haze for whatever part of the view starts below the sea, so a camera at the waterline splits along it.
	UMaterialInterface* underwater = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Environment/Continent/PP_Underwater.PP_Underwater"));
	if (underwater != nullptr)
	{
		_underwater = UMaterialInstanceDynamic::Create(underwater, this);
		_camera->PostProcessSettings.AddBlendable(_underwater, 0.0f);
	}

	// Held in the air until the ground streams in, or it would fall through the sea before the first section loads.
	GetCharacterMovement()->DisableMovement();

	UServerConnectionSubsystem* connection = connectionOf(*this);
	if (connection != nullptr)
	{
		connection->world().onCorrected.BindUObject(this, &AWorldCharacter::corrected);
	}
}

void AWorldCharacter::EndPlay(const EEndPlayReason::Type reason)
{
	UServerConnectionSubsystem* connection = connectionOf(*this);
	if (connection != nullptr)
	{
		connection->world().onCorrected.Unbind();
	}

	if (_metaHuman != nullptr)
	{
		_metaHuman->Destroy();
		_metaHuman = nullptr;
	}

	Super::EndPlay(reason);
}

void AWorldCharacter::wear(const Network::FAppearance& appearance)
{
	// The stand-in hides while the MetaHuman loads and comes back only when the MetaHuman assets are missing, so it never shows first and changes into the body.
	// The actor stands at the middle of its capsule, so the feet are half its height below.
	_body->SetVisibility(false);
	const TWeakObjectPtr<AWorldCharacter> weakThis(this);
	Appearance::attachTo(*this, appearance, FVector(0.0, 0.0, -capsuleHalfHeightCm), [weakThis](AActor* body)
	{
		AWorldCharacter* character = weakThis.Get();
		if (character == nullptr)
		{
			return;
		}

		if (body == nullptr)
		{
			character->_body->SetVisibility(true);
			return;
		}

		character->_metaHuman = body;
		character->updateViewHeight();
		character->_body->SetVisibility(false);
		body->SetActorHiddenInGame(character->_firstPerson);
	});
}

void AWorldCharacter::updateViewHeight()
{
	const double scale = (_metaHuman != nullptr) ? Appearance::heightScale(*_metaHuman) : 1.0;
	const double eyeHeight = capsuleHalfHeightCm + (_firstPerson ? firstPersonArmHeightCm : thirdPersonArmHeightCm);
	_arm->SetRelativeLocation(FVector(0.0, 0.0, eyeHeight * scale - capsuleHalfHeightCm));
	if (_portraitCapture != nullptr)
	{
		_portraitCapture->SetRelativeLocation(FVector(portraitDistanceCm * scale, 0.0, capsuleHalfHeightCm * (scale - 1.0)));
	}
}

UTextureRenderTarget2D* AWorldCharacter::beginPortrait()
{
	if (_portraitCapture == nullptr)
	{
		UTextureRenderTarget2D* target = NewObject<UTextureRenderTarget2D>(this);
		target->InitAutoFormat(portraitWidth, portraitHeight);

		// On the capsule, which turns with the body, so the camera always stands in front of the face looking back at it.
		_portraitCapture = NewObject<USceneCaptureComponent2D>(this);
		_portraitCapture->SetupAttachment(RootComponent);
		_portraitCapture->SetRelativeRotation(FRotator(0.0, 180.0, 0.0));
		updateViewHeight();
		_portraitCapture->FOVAngle = portraitFieldOfView;
		_portraitCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		_portraitCapture->TextureTarget = target;
		_portraitCapture->bCaptureEveryFrame = false;
		_portraitCapture->bCaptureOnMovement = false;
		_portraitCapture->RegisterComponent();
	}

	_portraitCapture->bCaptureEveryFrame = true;
	return _portraitCapture->TextureTarget;
}

void AWorldCharacter::endPortrait()
{
	// Filming the whole scene again is a second view's cost, so it stops the moment nobody looks at the picture.
	if (_portraitCapture != nullptr)
	{
		_portraitCapture->bCaptureEveryFrame = false;
	}
}

void AWorldCharacter::PawnClientRestart()
{
	// Before the parent's restart, because that is where SetupPlayerInputComponent binds them.
	// The movement actions are the rebindable assets the options screen lists. Look stays a raw mouse action made here, since no look action is rebindable.
	if (_lookMapping == nullptr)
	{
		_moveForwardAction = LoadObject<UInputAction>(nullptr, moveForwardPath);
		_moveBackwardAction = LoadObject<UInputAction>(nullptr, moveBackwardPath);
		_moveLeftAction = LoadObject<UInputAction>(nullptr, moveLeftPath);
		_moveRightAction = LoadObject<UInputAction>(nullptr, moveRightPath);
		_jumpAction = LoadObject<UInputAction>(nullptr, jumpPath);
		_autoRunAction = LoadObject<UInputAction>(nullptr, autoRunPath);
		_backViewAction = LoadObject<UInputAction>(nullptr, backViewPath);
		_switchCameraAction = LoadObject<UInputAction>(nullptr, switchCameraPath);

		_lookAction = NewObject<UInputAction>(this);
		_lookAction->ValueType = EInputActionValueType::Axis2D;

		_zoomAction = NewObject<UInputAction>(this);
		_zoomAction->ValueType = EInputActionValueType::Axis1D;

		_lookMapping = NewObject<UInputMappingContext>(this);
		_lookMapping->MapKey(_lookAction, EKeys::Mouse2D);
		_lookMapping->MapKey(_zoomAction, EKeys::MouseWheelAxis);
	}

	Super::PawnClientRestart();

	// Added beside the bootstrap's context rather than in place of it, which the controller applies once per level.
	APlayerController* controller = Cast<APlayerController>(GetController());
	UEnhancedInputLocalPlayerSubsystem* input = (controller != nullptr) ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(controller->GetLocalPlayer()) : nullptr;
	if (input != nullptr)
	{
		input->AddMappingContext(_lookMapping, 1);
	}
}

void AWorldCharacter::SetupPlayerInputComponent(UInputComponent* input)
{
	Super::SetupPlayerInputComponent(input);

	UEnhancedInputComponent* enhanced = Cast<UEnhancedInputComponent>(input);
	if (enhanced == nullptr)
	{
		return;
	}

	if ((_moveForwardAction == nullptr) || (_moveBackwardAction == nullptr) || (_moveLeftAction == nullptr) || (_moveRightAction == nullptr))
	{
		UE_LOG(LogTemp, Warning, TEXT("WorldCharacter is missing a movement action asset, so some movement keys do nothing."));
	}

	if (_moveForwardAction != nullptr)
	{
		enhanced->BindAction(_moveForwardAction, ETriggerEvent::Triggered, this, &AWorldCharacter::moveForward);
	}

	if (_moveBackwardAction != nullptr)
	{
		enhanced->BindAction(_moveBackwardAction, ETriggerEvent::Triggered, this, &AWorldCharacter::moveBackward);
	}

	if (_moveLeftAction != nullptr)
	{
		enhanced->BindAction(_moveLeftAction, ETriggerEvent::Triggered, this, &AWorldCharacter::moveLeft);
	}

	if (_moveRightAction != nullptr)
	{
		enhanced->BindAction(_moveRightAction, ETriggerEvent::Triggered, this, &AWorldCharacter::moveRight);
	}

	if (_jumpAction != nullptr)
	{
		enhanced->BindAction(_jumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		enhanced->BindAction(_jumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		enhanced->BindAction(_jumpAction, ETriggerEvent::Triggered, this, &AWorldCharacter::rise);
	}

	if (_autoRunAction != nullptr)
	{
		enhanced->BindAction(_autoRunAction, ETriggerEvent::Started, this, &AWorldCharacter::toggleAutoRun);
	}

	if (_backViewAction != nullptr)
	{
		enhanced->BindAction(_backViewAction, ETriggerEvent::Started, this, &AWorldCharacter::toggleRearView);
	}

	if (_switchCameraAction != nullptr)
	{
		enhanced->BindAction(_switchCameraAction, ETriggerEvent::Started, this, &AWorldCharacter::toggleFirstPerson);
	}

	if (_lookAction != nullptr)
	{
		enhanced->BindAction(_lookAction, ETriggerEvent::Triggered, this, &AWorldCharacter::look);
	}

	if (_zoomAction != nullptr)
	{
		enhanced->BindAction(_zoomAction, ETriggerEvent::Triggered, this, &AWorldCharacter::zoom);
	}
}

void AWorldCharacter::moveForward()
{
	move(1.0f, 0.0f);
}

void AWorldCharacter::moveBackward()
{
	move(-1.0f, 0.0f);
}

void AWorldCharacter::moveLeft()
{
	move(0.0f, -1.0f);
}

void AWorldCharacter::moveRight()
{
	move(0.0f, 1.0f);
}

void AWorldCharacter::move(float forward, float right)
{
	// Yaw only on land, so looking up does not walk the character into the ground; in water the pitch too, so looking down dives.
	// In rear view the camera faces back, so the half turn is undone and the keys still move the way the character faced.
	const float yaw = GetControlRotation().Yaw + (_rearView ? 180.0f : 0.0f);
	const FRotator facing(_swimming ? GetControlRotation().Pitch : 0.0f, yaw, 0.0f);

	AddMovementInput(FRotationMatrix(facing).GetUnitAxis(EAxis::X), forward);
	AddMovementInput(FRotationMatrix(facing).GetUnitAxis(EAxis::Y), right);
	_movedThisFrame = true;
}

void AWorldCharacter::rise()
{
	// On land the jump key jumps; in water, held, it swims straight up.
	if (_swimming)
	{
		AddMovementInput(FVector::UpVector, 1.0f);
		_movedThisFrame = true;
	}
}

void AWorldCharacter::toggleAutoRun()
{
	// One press starts it and the next stops it.
	_autoRun = !_autoRun;
}

void AWorldCharacter::toggleRearView()
{
	if (Controller == nullptr)
	{
		return;
	}

	// The camera follows the control rotation, so a half turn of it looks behind, and a second half turn looks ahead again.
	_rearView = !_rearView;
	FRotator rotation = Controller->GetControlRotation();
	rotation.Yaw += 180.0f;
	Controller->SetControlRotation(rotation);
}

void AWorldCharacter::toggleFirstPerson()
{
	_firstPerson = !_firstPerson;

	// From the eyes the body turns with the camera, as somebody looking about does; from behind it faces where it runs.
	bUseControllerRotationYaw = _firstPerson;
	GetCharacterMovement()->bOrientRotationToMovement = !_firstPerson;

	// The arm length glides in Tick; the height and the hidden body change at once, so the camera never sits inside the head.
	updateViewHeight();
	_body->SetHiddenInGame(_firstPerson);
	if (_metaHuman != nullptr)
	{
		_metaHuman->SetActorHiddenInGame(_firstPerson);
	}
}

bool AWorldCharacter::pressedOtherThanAutoRun(const APlayerController& controller) const
{
	// The auto run key toggles it itself, wherever the player rebound it, so it is left to that binding.
	TArray<FKey> autoRunKeys;
	const UEnhancedInputLocalPlayerSubsystem* input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(controller.GetLocalPlayer());
	if ((input != nullptr) && (_autoRunAction != nullptr))
	{
		autoRunKeys = input->QueryKeysMappedToAction(_autoRunAction);
	}

	// ponytail: every key the engine knows, scanned only on a frame where something went down while auto running.
	TArray<FKey> keys;
	EKeys::GetAllKeys(keys);
	for (const FKey& key : keys)
	{
		// Looking and zooming steer the run, so the mouse axes and the wheel do not stop it.
		const bool steers = key.IsAnalog() || (key == EKeys::MouseScrollUp) || (key == EKeys::MouseScrollDown);
		if (!steers && (key != EKeys::AnyKey) && !autoRunKeys.Contains(key) && controller.WasInputKeyJustPressed(key))
		{
			return true;
		}
	}

	return false;
}

void AWorldCharacter::look(const FInputActionValue& value)
{
	// Measured against the default sensitivity, so a fresh install turns exactly as it always did and the slider scales from there.
	const UGameSettings* settings = UGameSettings::get();
	const float scale = (settings != nullptr) ? (settings->getMouseSensitivity() / defaultMouseSensitivity) : 1.0f;

	const FVector2D axes = value.Get<FVector2D>();
	AddControllerYawInput(axes.X * scale);
	AddControllerPitchInput(-axes.Y * scale);
}

void AWorldCharacter::zoom(const FInputActionValue& value)
{
	// The wheel rolled forward reads positive and brings the camera in.
	_cameraDistanceCm = FMath::Clamp(_cameraDistanceCm - (value.Get<float>() * cameraZoomStepCm), cameraNearestCm, cameraFarthestCm);
}

void AWorldCharacter::Tick(float deltaSeconds)
{
	Super::Tick(deltaSeconds);

	if (!_onGround)
	{
		_onGround = settleOnGround();
	}

	const APlayerController* controller = Cast<APlayerController>(GetController());

	// Any other key, button or click stops auto run. The cheap any-key check comes first, so the full scan runs only on a press.
	if (_autoRun && (controller != nullptr) && controller->WasInputKeyJustPressed(EKeys::AnyKey) && pressedOtherThanAutoRun(*controller))
	{
		_autoRun = false;
	}

	// Auto run, or both mouse buttons held, run where the camera looks, as the forward key does, so in water they dive and climb with the camera too.
	const bool bothButtons = (controller != nullptr) && controller->IsInputKeyDown(EKeys::LeftMouseButton) && controller->IsInputKeyDown(EKeys::RightMouseButton);
	if (_autoRun || bothButtons)
	{
		moveForward();
	}

	// Read every frame, so a change made in the options screen over the world shows at once.
	const UGameSettings* settings = UGameSettings::get();
	if ((settings != nullptr) && (_camera->FieldOfView != settings->getFieldOfView()))
	{
		_camera->SetFieldOfView(settings->getFieldOfView());
	}

	// The haze pass runs only with the camera near the water, since higher up no part of the view can start below it.
	if (_underwater != nullptr)
	{
		const float cameraHeight = static_cast<float>(_camera->GetComponentLocation().Z - Water::surfaceCm);
		_underwater->SetScalarParameterValue(TEXT("CameraHeight"), cameraHeight);
		for (FWeightedBlendable& blendable : _camera->PostProcessSettings.WeightedBlendables.Array)
		{
			if (blendable.Object == _underwater)
			{
				blendable.Weight = (cameraHeight < 50.0f) ? 1.0f : 0.0f;
			}
		}
	}

	_arm->TargetArmLength = FMath::FInterpTo(_arm->TargetArmLength, _firstPerson ? 0.0f : _cameraDistanceCm, deltaSeconds, cameraZoomRate);

	if (_onGround)
	{
		swimOrStand(deltaSeconds);
	}

	// The worn body plays what the capsule is doing: swimming, in the air, standing or running.
	if (_metaHuman != nullptr)
	{
		const float speed = static_cast<float>(GetVelocity().Size2D());
		const bool still = (speed < standingSpeedCm);
		Appearance::EMotion motion = still ? Appearance::EMotion::Idle : Appearance::EMotion::Run;
		if (_swimming)
		{
			motion = still ? Appearance::EMotion::SwimIdle : Appearance::EMotion::Swim;
		}
		else if (GetCharacterMovement()->IsFalling())
		{
			motion = Appearance::EMotion::Fall;
		}

		Appearance::animate(*_metaHuman, motion, speed);
	}

	// The server places a character by its feet, and the actor stands at the middle of its capsule.
	// The movement bindings fire before this tick, so the flag says whether a key was held this frame.
	UServerConnectionSubsystem* connection = connectionOf(*this);
	if (connection != nullptr)
	{
		const FVector feet = GetActorLocation() - FVector(0.0f, 0.0f, capsuleHalfHeightCm);
		connection->world().setLocalState(feet, GetActorRotation().Yaw, _movedThisFrame);
	}

	_movedThisFrame = false;
}

void AWorldCharacter::swimOrStand(float deltaSeconds)
{
	// The ground under the capsule, or none past the island's edge, which is open sea.
	const FVector here = GetActorLocation();
	FCollisionQueryParams query;
	query.AddIgnoredActor(this);

	FHitResult hit;
	const bool found = GetWorld()->LineTraceSingleByChannel(hit, here, here - FVector(0.0, 0.0, settleSearchCm), ECC_WorldStatic, query);
	const bool deep = Water::tooDeep(found ? hit.ImpactPoint.Z : (Water::surfaceCm - settleSearchCm), _swimming);

	UCharacterMovementComponent* movement = GetCharacterMovement();
	// Wading out stands the body on the bottom at once. It floats at the swimmer line, a metre above it, and falling that far would play the jump.
	if (deep != _swimming)
	{
		_swimming = deep;
		_surfacing = deep;
		if (deep)
		{
			movement->SetMovementMode(MOVE_Flying);
		}
		else if (!settleOnGround())
		{
			movement->SetMovementMode(MOVE_Falling);
		}
	}

	if (!_swimming)
	{
		return;
	}

	// The capsule's middle is half its height above the feet, which float no higher than the water's swimmer line.
	// Wading in from the beach puts the swimmer on it at once, since the swim clip holds the head lower than a standing body and rising slowly would dip it under first; dropping in from above glides down to it.
	// After that it holds whatever depth it swims to, and that line is as high as it goes.
	const double floating = Water::swimmerFeetCm + capsuleHalfHeightCm;
	double height = FMath::Min(here.Z, floating);
	if (_surfacing)
	{
		height = (here.Z < floating) ? floating : FMath::FInterpTo(here.Z, floating, static_cast<double>(deltaSeconds), static_cast<double>(floatRate));
		_surfacing = (FMath::Abs(floating - height) > 2.0) && (movement->Velocity.Z >= 0.0);
	}

	if (height != here.Z)
	{
		SetActorLocation(FVector(here.X, here.Y, height));
	}

	if ((height >= floating) && (movement->Velocity.Z > 0.0))
	{
		movement->Velocity.Z = 0.0;
	}
}

bool AWorldCharacter::settleOnGround()
{
	const FVector here = GetActorLocation();
	FCollisionQueryParams query;
	query.AddIgnoredActor(this);

	FHitResult hit;
	if (!GetWorld()->LineTraceSingleByChannel(hit, here + FVector(0.0, 0.0, settleSearchCm), here - FVector(0.0, 0.0, settleSearchCm), ECC_WorldStatic, query))
	{
		return false;
	}

	SetActorLocation(hit.ImpactPoint + FVector(0.0, 0.0, capsuleHalfHeightCm + 2.0), false, nullptr, ETeleportType::TeleportPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	return true;
}

void AWorldCharacter::corrected(const Network::FObjectPlace& place, bool teleport)
{
	SetActorLocation(place.location + FVector(0.0f, 0.0f, capsuleHalfHeightCm), false, nullptr, teleport ? ETeleportType::TeleportPhysics : ETeleportType::None);

	if (teleport)
	{
		SetActorRotation(FRotator(0.0f, place.yaw, 0.0f));
	}
}
