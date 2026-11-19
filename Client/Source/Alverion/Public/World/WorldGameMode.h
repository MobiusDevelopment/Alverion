#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameServer/GameServerPackets.h"
#include "GameServer/WorldFlow.h"

#include "WorldGameMode.generated.h"

class AProxyCharacter;
class AWorldCharacter;
class URootLayout;
class UServerConnectionSubsystem;

/** The continent's game mode: the player's character where the server put it, and everybody the server says is around. */
UCLASS()
class ALVERION_API AWorldGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWorldGameMode();

	/** Takes the character out of the world for the character list, keeping the link, and travels to it once the server agrees. */
	void returnToCharacterSelect();

	/**
	 * The townsperson the character is facing, for the interact key: the nearest one within talking range and in front.
	 * Returns zero when there is none. Monsters and players are never picked.
	 */
	uint32 townspersonInFront(const AActor& character) const;

	/** How far a creature the server has told this client about stands from the actor, flat, in centimetres. False when the creature is not one it knows. */
	bool distanceToCreature(uint32 objectId, const AActor& from, double& distanceCm) const;

	/**
	 * The monster a swing from the place given, towards the direction given, would reach: the nearest standing one within reach and in front.
	 * Returns zero when there is none. Townspeople and players are never picked.
	 */
	uint32 monsterInFront(const FVector& origin, const FVector& forward, double reachCm) const;

	/** The fallen monster the character is standing over, for the interact key to loot: the nearest one within reach and in front. Zero when there is none. */
	uint32 corpseInFront(const AActor& character) const;

	/** The name the server gives a creature it has told this client about, empty for one it has not. */
	FString nameOf(uint32 objectId) const;

	/** Shows a fallen player the death menu, whose Respawn asks the server for the nearest village. */
	void offerReturnToVillage();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type reason) override;

private:
	void spawned(const Network::FCharacterSpawn& spawn);
	void despawned(uint32 objectId);
	void moved(const Network::FObjectPlace& place, Network::EMoveKind kind);
	void moveTypeChanged(uint32 objectId, bool running);
	void attacked(const Network::FAttack& attack);
	void lifeChanged(uint32 objectId, bool alive);
	void stanceChanged(uint32 objectId, bool inCombat);
	void lootReceived(const Network::FCorpseLoot& loot);
	UServerConnectionSubsystem* connection() const;

	/** Which creatures a pick in front of the character looks for. */
	enum class EPick : uint8
	{
		Townsperson,
		Monster,
		Corpse
	};

	/** The creature of the kind given nearest the place, within reach and in front. */
	uint32 inFront(const FVector& origin, const FVector& forward, double reachCm, EPick pick) const;

	/** Where a creature stands, this client's own character included. False for one it does not know. */
	bool locationOf(uint32 objectId, FVector& location) const;

	/** This client's own character, when it has one in this world. */
	AWorldCharacter* localCharacter() const;
	uint32 localObjectId() const;

	UPROPERTY()
	TMap<uint32, TObjectPtr<AProxyCharacter>> _proxies;

	UPROPERTY()
	TObjectPtr<URootLayout> _rootLayout;
};
