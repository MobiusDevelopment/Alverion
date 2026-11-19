#include "World/DamageNumbersComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/TextRenderComponent.h"
#include "GameServer/ServerPackets/Combat.h"
#include "Kismet/GameplayStatics.h"
#include "UI/LocalizedText.h"

namespace
{
	// How long a number stays up, how far it rises meanwhile, and the last part of that over which it shrinks away.
	constexpr float damageNumberSeconds = 1.2f;
	constexpr float damageNumberRiseCm = 70.0f;
	constexpr float damageNumberShrinkSeconds = 0.4f;

	constexpr float damageNumberSize = 30.0f;
	constexpr float damageNumberCriticalSize = 44.0f;
	const FColor damageNumberColor(255, 240, 225);
	const FColor damageNumberCriticalColor(255, 196, 64);
	const FColor damageNumberMissColor(170, 170, 170);
}

UDamageNumbersComponent::UDamageNumbersComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDamageNumbersComponent::show(int32 damage, uint8 flags)
{
	AActor* owner = GetOwner();
	if (owner == nullptr)
	{
		return;
	}

	const bool missed = (flags & Network::GameServer::hitMissed) != 0;
	const bool critical = !missed && ((flags & Network::GameServer::hitCritical) != 0);

	UTextRenderComponent* text = NewObject<UTextRenderComponent>(owner);
	text->SetupAttachment(this);
	text->SetHorizontalAlignment(EHTA_Center);
	text->SetText(missed ? LocalizedText::get(TEXT("combat.miss")) : FText::AsNumber(damage));
	text->SetWorldSize(critical ? damageNumberCriticalSize : damageNumberSize);
	text->SetTextRenderColor(missed ? damageNumberMissColor : (critical ? damageNumberCriticalColor : damageNumberColor));

	// A little to one side or the other, so blows in quick succession do not print over each other.
	text->SetRelativeLocation(FVector(0.0, FMath::FRandRange(-20.0, 20.0), 0.0));
	text->RegisterComponent();
	_floating.Emplace(text, 0.0f);
}

void UDamageNumbersComponent::TickComponent(float deltaSeconds, ELevelTick tickType, FActorComponentTickFunction* thisTickFunction)
{
	Super::TickComponent(deltaSeconds, tickType, thisTickFunction);

	const APlayerCameraManager* camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	for (int32 index = _floating.Num() - 1; index >= 0; index--)
	{
		UTextRenderComponent* text = _floating[index].Key.Get();
		float& age = _floating[index].Value;
		age += deltaSeconds;
		if ((text == nullptr) || (age >= damageNumberSeconds))
		{
			if (text != nullptr)
			{
				text->DestroyComponent();
			}

			_floating.RemoveAt(index);
			continue;
		}

		const FVector place = text->GetRelativeLocation();
		text->SetRelativeLocation(FVector(place.X, place.Y, damageNumberRiseCm * (age / damageNumberSeconds)));
		const float left = damageNumberSeconds - age;
		text->SetRelativeScale3D(FVector((left < damageNumberShrinkSeconds) ? (left / damageNumberShrinkSeconds) : 1.0f));

		// The number turns to whoever is looking, as the nameplate does.
		if (camera != nullptr)
		{
			const FVector toCamera = camera->GetCameraLocation() - text->GetComponentLocation();
			text->SetWorldRotation(FRotator(0.0f, toCamera.Rotation().Yaw, 0.0f));
		}
	}
}
