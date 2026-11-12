#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "HeadwearHairComponent.generated.h"

class UGroomComponent;
class UMeshComponent;
class USkeletalMeshComponent;

/** Keeps a hair-card clipping plane attached to the opening of headwear as the head moves. */
UCLASS(Transient, NotBlueprintable)
class ALVERION_API UHeadwearHairComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHeadwearHairComponent();
	void configure(UGroomComponent* hair, USkeletalMeshComponent* body, UMeshComponent* headwear, float openingOffsetCm = 1.0f);
	virtual void TickComponent(float deltaTime, ELevelTick tickType, FActorComponentTickFunction* tickFunction) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UGroomComponent> _hair;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> _body;

	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> _headwear;

	FVector _openingPoint = FVector::ZeroVector;
	FVector _openingNormal = FVector::UpVector;
};
