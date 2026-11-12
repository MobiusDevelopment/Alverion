#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameServer/GameServerPackets.h"

#include "ProxyCharacter.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Somebody or something else in the world: another player, a townsperson or a monster.
 * The server sends a destination and lets the client walk there, so this walks at the mover's own speed and keeps its feet on the island.
 */
UCLASS()
class ALVERION_API AProxyCharacter : public AActor
{
	GENERATED_BODY()

public:
	AProxyCharacter();

	void setup(const Network::FCharacterSpawn& spawn);
	void walkTo(const FVector& destination);
	void standAt(const FVector& location, float yaw);

	/** Whether this is a townsperson: a non-player that the server does not let the player attack, and so one that can be talked to. */
	bool isTownsperson() const;

	/** Switches between the walk and the run, which move at different speeds and play different clips. */
	void setRunning(bool running);

	virtual void Tick(float deltaSeconds) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type reason) override;

private:
	/** Where this stands at a place: on the island, or at the water line where the island lies too deep to stand on, which sets it swimming. */
	FVector settle(const FVector& location);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> _body;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> _label;

	/** The MetaHuman this wears, riding on this actor: a player's own look, or the default look on townspeople and monsters. */
	UPROPERTY()
	TObjectPtr<AActor> _metaHuman;

	/** The bytes the MetaHuman was dressed from, so a repeated spawn does not stand up a second body. */
	TArray<uint8> _wornAppearance;
	bool _wearsGoblin = false;

	/** The garments the body shows, from the items the server last showed on this player. */
	uint8 _worn = 0;
	bool _townsperson = false;
	FQuat _bodyRestRotation = FQuat::Identity;
	FVector _groundNormal = FVector::UpVector;

	FVector _destination = FVector::ZeroVector;
	float _speed = 0.0f;
	float _walkSpeed = 0.0f;
	float _swimSpeed = 0.0f;
	float _groundRetrySeconds = 0.0f;
	bool _running = true;
	bool _walking = false;
	bool _swimming = false;
};
