#include "World/WorldGameMode.h"

#include "Core/ClientPlayerController.h"
#include "Engine/World.h"
#include "Core/GameSettings.h"
#include "Kismet/GameplayStatics.h"
#include "World/ProxyCharacter.h"
#include "Core/MusicSubsystem.h"
#include "UI/ActionBarWidget.h"
#include "UI/ArrivalCover.h"
#include "UI/ChatWidget.h"
#include "UI/RootLayout.h"
#include "UI/LocalizedText.h"
#include "UI/StatusBarsWidget.h"
#include "UI/TargetFrameWidget.h"
#include "UI/UISubsystem.h"
#include "ServerConnectionSubsystem.h"
#include "TimerManager.h"
#include "World/WorldCharacter.h"

namespace
{
	// Half the character's capsule, plus a little so it starts above the server's height rather than inside the ground.
	constexpr double spawnLiftCm = 100.0;

	// The server lets a townsperson be talked to within 250 of its units. The character has to be standing next to one, so this is a good deal nearer.
	constexpr double talkRangeCm = 55.0 * Network::worldUnitCm;

	// Sixty degrees either side of the way the character faces. The server asks for no direction at all, so this only keeps the pick to what is ahead.
	constexpr double talkConeCosine = 0.5;

	// The server sends an Attack as the swing starts and the blow lands partway through it, which is when the struck flinch and their numbers show.
	constexpr float worldAttackLandSeconds = 0.35f;

	// The server lets a corpse be looted within 150 of its units. The character stands over it to loot, so this is nearer.
	constexpr double worldLootRangeCm = 60.0 * Network::worldUnitCm;
}

AWorldGameMode::AWorldGameMode()
{
	PlayerControllerClass = AClientPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
}

UServerConnectionSubsystem* AWorldGameMode::connection() const
{
	UGameInstance* instance = GetGameInstance();
	return (instance != nullptr) ? instance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
}

void AWorldGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* world = GetWorld();
	UServerConnectionSubsystem* link = connection();
	if ((world == nullptr) || (link == nullptr))
	{
		return;
	}

	// The display, sound and language settings, for a world started without passing the front end.
	UGameSettings* settings = UGameSettings::get();
	if (settings != nullptr)
	{
		settings->ApplySettings(false);
	}

	// The layers the game menu and its options open on. A layout does not survive travel, so every level makes its own.
	_rootLayout = CreateWidget<URootLayout>(world, URootLayout::StaticClass());
	if (_rootLayout != nullptr)
	{
		_rootLayout->AddToViewport();
	}

	// Under the layout, so every screen opened over the world covers it.
	UActionBarWidget* actionBar = CreateWidget<UActionBarWidget>(world, UActionBarWidget::StaticClass());
	if (actionBar != nullptr)
	{
		actionBar->AddToViewport(-1);
	}

	UChatWidget* chat = CreateWidget<UChatWidget>(world, UChatWidget::StaticClass());
	if (chat != nullptr)
	{
		chat->AddToViewport(-1);
	}

	UStatusBarsWidget* statusBars = CreateWidget<UStatusBarsWidget>(world, UStatusBarsWidget::StaticClass());
	if (statusBars != nullptr)
	{
		statusBars->AddToViewport(-1);
	}

	UTargetFrameWidget* targetFrame = CreateWidget<UTargetFrameWidget>(world, UTargetFrameWidget::StaticClass());
	if (targetFrame != nullptr)
	{
		targetFrame->AddToViewport(-1);
	}

	// Black for a second while the world draws, then a fade.
	UArrivalCover* cover = CreateWidget<UArrivalCover>(world, UArrivalCover::StaticClass());
	if (cover != nullptr)
	{
		cover->AddToViewport(UArrivalCover::coverZOrder);
	}

	// The menu theme played on through the loading screen and gives way to the world here.
	UMusicSubsystem* music = GetGameInstance()->GetSubsystem<UMusicSubsystem>();
	if (music != nullptr)
	{
		music->stopMenuTheme();
	}

	FActorSpawnParameters parameters;
	parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// The map's ground streams in after this, so the character waits where the server put it until there is ground under it.
	const Network::FCharacterSelected& selected = link->gameServer().selected();
	AWorldCharacter* character = world->SpawnActor<AWorldCharacter>(selected.location + FVector(0.0, 0.0, spawnLiftCm), FRotator::ZeroRotator, parameters);

	// The server does not repeat our own appearance once we are in, so it comes from the character list the selection was made from.
	const Network::FCharacterListEntry* entry = link->gameServer().characters().FindByPredicate([&selected](const Network::FCharacterListEntry& candidate)
	{
		return candidate.objectId == selected.objectId;
	});

	Network::FAppearance appearance;
	if ((character != nullptr) && (entry != nullptr) && Network::FAppearance::unpack(entry->appearance, appearance))
	{
		character->wear(appearance);
	}

	APlayerController* controller = UGameplayStatics::GetPlayerController(this, 0);
	if ((controller != nullptr) && (character != nullptr))
	{
		controller->Possess(character);
		controller->SetShowMouseCursor(false);
		controller->SetInputMode(FInputModeGameOnly());
	}

	Network::FWorldFlow& flow = link->world();
	flow.onSpawned.BindUObject(this, &AWorldGameMode::spawned);
	flow.onDespawned.BindUObject(this, &AWorldGameMode::despawned);
	flow.onMoved.BindUObject(this, &AWorldGameMode::moved);
	flow.onMoveTypeChanged.BindUObject(this, &AWorldGameMode::moveTypeChanged);
	flow.onAttack.BindUObject(this, &AWorldGameMode::attacked);
	flow.onLifeChanged.BindUObject(this, &AWorldGameMode::lifeChanged);
	flow.onCombatStance.BindUObject(this, &AWorldGameMode::stanceChanged);
	flow.onCorpseLoot.BindUObject(this, &AWorldGameMode::lootReceived);

	// A lost link goes back to the login screen, which is what the front end shows for anything but a character list.
	link->gameServer().onFailed.BindWeakLambda(this, [this](const FString& reason)
	{
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/L_Login")));
	});

	flow.begin(link->gameServer());
}

void AWorldGameMode::EndPlay(const EEndPlayReason::Type reason)
{
	UServerConnectionSubsystem* link = connection();
	if (link != nullptr)
	{
		Network::FWorldFlow& flow = link->world();
		flow.onSpawned.Unbind();
		flow.onDespawned.Unbind();
		flow.onMoved.Unbind();
		flow.onMoveTypeChanged.Unbind();
		flow.onAttack.Unbind();
		flow.onLifeChanged.Unbind();
		flow.onCombatStance.Unbind();
		flow.onCorpseLoot.Unbind();
		flow.end();
		link->gameServer().onFailed.Unbind();
		link->gameServer().onStepChanged.Unbind();
		link->gameServer().onRestartRefused.Unbind();
	}

	Super::EndPlay(reason);
}

void AWorldGameMode::returnToCharacterSelect()
{
	UServerConnectionSubsystem* link = connection();
	if (link == nullptr)
	{
		return;
	}

	// The front end opens on the character list when the link already holds one, so arriving there is all that is left.
	link->gameServer().onStepChanged.BindWeakLambda(this, [this](Network::EGameServerStep step)
	{
		if (step == Network::EGameServerStep::CharacterSelect)
		{
			UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/L_Login")));
		}
	});

	link->gameServer().onRestartRefused.BindWeakLambda(this, [this]()
	{
		UE_LOG(LogTemp, Display, TEXT("The server kept the character in the world, as it does in combat."));
	});

	link->gameServer().returnToCharacterSelect();
}

uint32 AWorldGameMode::townspersonInFront(const AActor& character) const
{
	return inFront(character.GetActorLocation(), character.GetActorForwardVector(), talkRangeCm, EPick::Townsperson);
}

uint32 AWorldGameMode::monsterInFront(const FVector& origin, const FVector& forward, double reachCm) const
{
	return inFront(origin, forward, reachCm, EPick::Monster);
}

uint32 AWorldGameMode::corpseInFront(const AActor& character) const
{
	return inFront(character.GetActorLocation(), character.GetActorForwardVector(), worldLootRangeCm, EPick::Corpse);
}

uint32 AWorldGameMode::inFront(const FVector& origin, const FVector& forwardDirection, double reachCm, EPick pick) const
{
	const FVector forward = forwardDirection.GetSafeNormal2D();
	uint32 nearestObjectId = 0;
	double nearestDistance = reachCm;
	for (const TPair<uint32, TObjectPtr<AProxyCharacter>>& proxy : _proxies)
	{
		if (proxy.Value == nullptr)
		{
			continue;
		}

		const bool monster = proxy.Value->isMonster();
		const bool fallen = proxy.Value->isFallen();
		const bool wanted = (pick == EPick::Townsperson) ? proxy.Value->isTownsperson() : (pick == EPick::Monster) ? (monster && !fallen) : (monster && fallen);
		if (!wanted)
		{
			continue;
		}

		// Flat distance, since the ground under the two is the client's own and the server has the last word on the height.
		const FVector offset = proxy.Value->GetActorLocation() - origin;
		const double distance = offset.Size2D();
		if ((distance < nearestDistance) && (FVector::DotProduct(forward, offset.GetSafeNormal2D()) >= talkConeCosine))
		{
			nearestObjectId = proxy.Key;
			nearestDistance = distance;
		}
	}

	return nearestObjectId;
}

FString AWorldGameMode::nameOf(uint32 objectId) const
{
	const TObjectPtr<AProxyCharacter>* proxy = _proxies.Find(objectId);
	return ((proxy != nullptr) && (*proxy != nullptr)) ? (*proxy)->displayName() : FString();
}

AWorldCharacter* AWorldGameMode::localCharacter() const
{
	return Cast<AWorldCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
}

uint32 AWorldGameMode::localObjectId() const
{
	UServerConnectionSubsystem* link = connection();
	return (link != nullptr) ? link->gameServer().selected().objectId : 0;
}

bool AWorldGameMode::locationOf(uint32 objectId, FVector& location) const
{
	if (objectId == localObjectId())
	{
		const AWorldCharacter* character = localCharacter();
		if (character != nullptr)
		{
			location = character->GetActorLocation();
		}

		return character != nullptr;
	}

	const TObjectPtr<AProxyCharacter>* proxy = _proxies.Find(objectId);
	if ((proxy == nullptr) || (*proxy == nullptr))
	{
		return false;
	}

	location = (*proxy)->GetActorLocation();
	return true;
}

void AWorldGameMode::attacked(const Network::FAttack& attack)
{
	if (attack.hits.IsEmpty())
	{
		return;
	}

	// The attacker swings at once, turned to the first one it strikes.
	FVector targetLocation = FVector::ZeroVector;
	const bool targetKnown = locationOf(attack.hits[0].targetId, targetLocation);
	if (attack.attackerId == localObjectId())
	{
		AWorldCharacter* character = localCharacter();
		if ((character != nullptr) && targetKnown)
		{
			character->strike(targetLocation);
		}
		else if (character != nullptr)
		{
			character->strikeAir();
		}
	}
	else
	{
		const TObjectPtr<AProxyCharacter>* attacker = _proxies.Find(attack.attackerId);
		if ((attacker != nullptr) && (*attacker != nullptr))
		{
			(*attacker)->strike(targetKnown ? targetLocation : (*attacker)->GetActorLocation());
		}
	}

	// The blows land a moment into the swing. Whoever was struck is looked up then, since a creature can leave in between.
	const TArray<Network::FAttackHit> hits = attack.hits;
	FTimerHandle landing;
	GetWorldTimerManager().SetTimer(landing, FTimerDelegate::CreateWeakLambda(this, [this, hits]()
	{
		const uint32 self = localObjectId();
		for (const Network::FAttackHit& hit : hits)
		{
			if (hit.targetId == self)
			{
				AWorldCharacter* character = localCharacter();
				if (character != nullptr)
				{
					character->takeHit(hit.damage, hit.flags);
				}

				continue;
			}

			const TObjectPtr<AProxyCharacter>* struck = _proxies.Find(hit.targetId);
			if ((struck != nullptr) && (*struck != nullptr))
			{
				(*struck)->takeHit(hit.damage, hit.flags);
			}
		}
	}), worldAttackLandSeconds, false);
}

void AWorldGameMode::lifeChanged(uint32 objectId, bool alive)
{
	if (objectId == localObjectId())
	{
		AWorldCharacter* character = localCharacter();
		if (character != nullptr)
		{
			character->setFallen(!alive);
		}

		AClientPlayerController* controller = Cast<AClientPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
		if (!alive)
		{
			offerReturnToVillage();
		}
		else if (controller != nullptr)
		{
			controller->closeDeathMenu();
		}

		return;
	}

	const TObjectPtr<AProxyCharacter>* proxy = _proxies.Find(objectId);
	if ((proxy != nullptr) && (*proxy != nullptr))
	{
		(*proxy)->setFallen(!alive);
	}
}

void AWorldGameMode::stanceChanged(uint32 objectId, bool inCombat)
{
	if (objectId == localObjectId())
	{
		AWorldCharacter* character = localCharacter();
		if (character != nullptr)
		{
			character->setInCombat(inCombat);
		}

		return;
	}

	const TObjectPtr<AProxyCharacter>* proxy = _proxies.Find(objectId);
	if ((proxy != nullptr) && (*proxy != nullptr))
	{
		(*proxy)->setInCombat(inCombat);
	}
}

void AWorldGameMode::lootReceived(const Network::FCorpseLoot& loot)
{
	AClientPlayerController* controller = Cast<AClientPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	if (controller != nullptr)
	{
		controller->showLoot(loot, FText::FromString(nameOf(loot.corpseId)));
	}
}

void AWorldGameMode::offerReturnToVillage()
{
	const AWorldCharacter* character = localCharacter();
	AClientPlayerController* controller = Cast<AClientPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	if ((character == nullptr) || !character->isFallen() || (controller == nullptr))
	{
		return;
	}

	// Respawn asks for the nearest village, and the server's Revive gets the character up there.
	const TWeakObjectPtr<AWorldGameMode> weakThis(this);
	controller->showDeathMenu([weakThis]()
	{
		AWorldGameMode* mode = weakThis.Get();
		UServerConnectionSubsystem* link = (mode != nullptr) ? mode->connection() : nullptr;
		if (link != nullptr)
		{
			link->world().returnToVillage();
		}
	});
}

bool AWorldGameMode::distanceToCreature(uint32 objectId, const AActor& from, double& distanceCm) const
{
	const TObjectPtr<AProxyCharacter>* proxy = _proxies.Find(objectId);
	if ((proxy == nullptr) || (*proxy == nullptr))
	{
		return false;
	}

	distanceCm = FVector::Dist2D((*proxy)->GetActorLocation(), from.GetActorLocation());
	return true;
}

void AWorldGameMode::spawned(const Network::FCharacterSpawn& spawn)
{
	// A spawn for somebody already here is the server repeating itself, so the existing one is moved and dressed again.
	TObjectPtr<AProxyCharacter>* existing = _proxies.Find(spawn.objectId);
	if ((existing != nullptr) && (*existing != nullptr))
	{
		(*existing)->setup(spawn);
		return;
	}

	FActorSpawnParameters parameters;
	parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AProxyCharacter* proxy = GetWorld()->SpawnActor<AProxyCharacter>(spawn.location, FRotator::ZeroRotator, parameters);
	if (proxy != nullptr)
	{
		proxy->setup(spawn);
		_proxies.Add(spawn.objectId, proxy);
	}
}

void AWorldGameMode::despawned(uint32 objectId)
{
	TObjectPtr<AProxyCharacter> proxy;
	if (_proxies.RemoveAndCopyValue(objectId, proxy) && (proxy != nullptr))
	{
		proxy->Destroy();
	}
}

void AWorldGameMode::moveTypeChanged(uint32 objectId, bool running)
{
	TObjectPtr<AProxyCharacter>* proxy = _proxies.Find(objectId);
	if ((proxy != nullptr) && (*proxy != nullptr))
	{
		(*proxy)->setRunning(running);
	}
}

void AWorldGameMode::moved(const Network::FObjectPlace& place, Network::EMoveKind kind)
{
	TObjectPtr<AProxyCharacter>* proxy = _proxies.Find(place.objectId);
	if ((proxy == nullptr) || (*proxy == nullptr))
	{
		return;
	}

	if (kind == Network::EMoveKind::Destination)
	{
		(*proxy)->walkTo(place.location);
	}
	else
	{
		(*proxy)->standAt(place.location, place.yaw);
	}
}
