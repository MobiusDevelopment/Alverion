#include "World/ProxyCharacter.h"

#include "Appearance.h"
#include "Appearance/AppearanceApplier.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Core/GameSettings.h"
#include "Kismet/GameplayStatics.h"
#include "World/SceneSetup.h"
#include "World/Water.h"

namespace
{
	const FLinearColor playerColor(0.35f, 0.6f, 1.0f);
	const FLinearColor townspersonColor(0.95f, 0.8f, 0.25f);
	const FLinearColor monsterColor(0.85f, 0.2f, 0.15f);

	// The body's centre and the label sit above the feet the server reports.
	constexpr float bodyCentreCm = 90.0f;
	constexpr float labelHeightCm = 215.0f;

	// How far above and below a reported height to look for the island.
	constexpr double groundSearchCm = 20000.0;
	constexpr float groundRetryIntervalSeconds = 0.5f;
}

AProxyCharacter::AProxyCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Feet"));

	_body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	_body->SetupAttachment(RootComponent);
	_body->SetRelativeLocation(FVector(0.0f, 0.0f, bodyCentreCm));

	_label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	_label->SetupAttachment(RootComponent);
	_label->SetRelativeLocation(FVector(0.0f, 0.0f, labelHeightCm));
	_label->SetHorizontalAlignment(EHTA_Center);
	_label->SetWorldSize(24.0f);
}

void AProxyCharacter::setup(const Network::FCharacterSpawn& spawn)
{
	const FLinearColor color = !spawn.isNpc ? playerColor : (spawn.attackable ? monsterColor : townspersonColor);
	SceneSetup::paintPlaceholderBody(*_body, color);

	_label->SetText(FText::FromString(spawn.name));
	_label->SetTextRenderColor(color.ToFColor(true));

	_townsperson = spawn.isNpc && !spawn.attackable;
	_speed = spawn.runSpeed;
	_walkSpeed = spawn.walkSpeed;
	_swimSpeed = spawn.swimSpeed;
	_running = spawn.running;
	standAt(spawn.location, spawn.yaw);

	const bool goblin = spawn.isNpc && spawn.attackable;
	Network::FAppearance appearance;
	appearance.male = !spawn.attackable;
	const TArray<uint8> look = spawn.isNpc ? appearance.pack() : spawn.appearance;
	if (((look == _wornAppearance) && (goblin == _wearsGoblin)) || !Network::FAppearance::unpack(look, appearance))
	{
		return;
	}

	_wornAppearance = look;
	_wearsGoblin = goblin;
	if (_metaHuman != nullptr)
	{
		_metaHuman->Destroy();
		_metaHuman = nullptr;
	}

	// The root is the feet, so the body stands on it.
	// The placeholder hides while the MetaHuman loads and comes back only when the MetaHuman assets are missing, so it never shows first and changes into the body.
	_body->SetVisibility(false);
	const TWeakObjectPtr<AProxyCharacter> weakThis(this);
	Appearance::attachTo(*this, appearance, FVector::ZeroVector, [weakThis, look, goblin](AActor* body)
	{
		AProxyCharacter* proxy = weakThis.Get();
		if (body == nullptr)
		{
			if ((proxy != nullptr) && (proxy->_metaHuman == nullptr))
			{
				proxy->_body->SetVisibility(true);
			}

			return;
		}

		// A slower load can finish after the proxy has received another appearance.
		if ((proxy == nullptr) || (proxy->_wearsGoblin != goblin) || (proxy->_wornAppearance != look) || (proxy->_metaHuman != nullptr))
		{
			body->Destroy();
			return;
		}

		proxy->_metaHuman = body;
		proxy->_bodyRestRotation = body->GetRootComponent()->GetRelativeRotation().Quaternion();
		proxy->_body->SetVisibility(false);
	}, goblin ? TEXT("MH_Goblin") : TEXT(""));
}

bool AProxyCharacter::isTownsperson() const
{
	return _townsperson;
}

void AProxyCharacter::EndPlay(const EEndPlayReason::Type reason)
{
	// Attached actors outlive their parent, so the body goes with the proxy.
	if (_metaHuman != nullptr)
	{
		_metaHuman->Destroy();
		_metaHuman = nullptr;
	}

	Super::EndPlay(reason);
}

void AProxyCharacter::walkTo(const FVector& destination)
{
	_destination = destination;
	_walking = true;
}

void AProxyCharacter::setRunning(bool running)
{
	_running = running;
}

void AProxyCharacter::standAt(const FVector& location, float yaw)
{
	_walking = false;
	SetActorLocation(settle(location));
	SetActorRotation(FRotator(0.0f, yaw, 0.0f));
}

FVector AProxyCharacter::settle(const FVector& location)
{
	FHitResult hit;
	FCollisionQueryParams query;
	query.AddIgnoredActor(this);

	// No island under a place is open sea.
	const FVector above = location + FVector(0.0, 0.0, groundSearchCm);
	const FVector below = location - FVector(0.0, 0.0, groundSearchCm);
	const bool found = GetWorld()->LineTraceSingleByChannel(hit, above, below, ECC_WorldStatic, query);
	_groundNormal = found ? FVector(hit.ImpactNormal) : FVector::UpVector;
	_groundRetrySeconds = found ? 0.0f : groundRetryIntervalSeconds;
	_swimming = Water::tooDeep(found ? hit.ImpactPoint.Z : (Water::surfaceCm - groundSearchCm), _swimming);

	// The root is the feet. A swimmer keeps the depth the server reports, no lower than the seabed and no higher than the swimmer line.
	if (_swimming)
	{
		const double seabed = found ? hit.ImpactPoint.Z : (Water::surfaceCm - groundSearchCm);
		return FVector(location.X, location.Y, FMath::Clamp(location.Z, seabed, Water::swimmerFeetCm));
	}

	return found ? hit.ImpactPoint : location;
}

void AProxyCharacter::Tick(float deltaSeconds)
{
	Super::Tick(deltaSeconds);

	// Terrain collision can stream in after a stationary proxy has already been placed at sea level.
	if (!_walking && (_groundRetrySeconds > 0.0f))
	{
		_groundRetrySeconds -= deltaSeconds;
		if (_groundRetrySeconds <= 0.0f)
		{
			SetActorLocation(settle(GetActorLocation()));
		}
	}

	// A server too old to report a swim speed leaves it zero, and the land speed stands in.
	const float landSpeed = _running ? _speed : _walkSpeed;
	const float speed = (_swimming && (_swimSpeed > 0.0f)) ? _swimSpeed : landSpeed;

	// The short goblin uses floor contacts baked for its leg length on land.
	if (_metaHuman != nullptr)
	{
		Appearance::EMotion motion = _walking ? (_wearsGoblin ? Appearance::EMotion::GoblinMove : Appearance::EMotion::Move) : (_wearsGoblin ? Appearance::EMotion::GoblinIdle : Appearance::EMotion::Idle);
		if (_swimming)
		{
			motion = _walking ? Appearance::EMotion::Swim : Appearance::EMotion::SwimIdle;
		}

		Appearance::animate(*_metaHuman, motion, speed);
	}

	if (_walking)
	{
		const FVector here = GetActorLocation();
		const double step = static_cast<double>(speed * deltaSeconds);

		// On land the ground carries the height; a swimmer heads for the destination's depth too, and tilts into a dive or a rise.
		const FVector toGo = _swimming ? (FVector(_destination.X, _destination.Y, FMath::Min(_destination.Z, Water::swimmerFeetCm)) - here) : FVector(_destination.X - here.X, _destination.Y - here.Y, 0.0);
		if ((toGo.Size() <= step) || (speed <= 0.0f))
		{
			standAt(_destination, GetActorRotation().Yaw);
		}
		else
		{
			const FVector direction = toGo.GetSafeNormal();
			SetActorLocation(settle(here + (direction * step)));
			SetActorRotation(_swimming ? direction.Rotation() : FRotator(0.0f, direction.Rotation().Yaw, 0.0f));
		}
	}

	// The visual body follows the surface under its feet. The proxy and nameplate stay upright on land.
	if (_wearsGoblin && (_metaHuman != nullptr))
	{
		const FVector up = _swimming ? GetActorUpVector() : _groundNormal;
		if (!_metaHuman->GetActorUpVector().Equals(up, UE_KINDA_SMALL_NUMBER))
		{
			const FVector localUp = GetActorQuat().UnrotateVector(up);
			_metaHuman->SetActorRelativeRotation(FQuat::FindBetweenNormals(FVector::UpVector, localUp) * _bodyRestRotation);
		}
	}

	// The options screen's nameplate switch, read every frame so turning it shows at once.
	const UGameSettings* settings = UGameSettings::get();
	const bool showName = (settings == nullptr) || settings->getShowNameplates();
	if (_label->IsVisible() != showName)
	{
		_label->SetVisibility(showName);
	}

	// The name turns to whoever is looking.
	const APlayerCameraManager* camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (showName && (camera != nullptr))
	{
		const FVector toCamera = camera->GetCameraLocation() - _label->GetComponentLocation();
		_label->SetWorldRotation(FRotator(0.0f, toCamera.Rotation().Yaw, 0.0f));
	}
}
