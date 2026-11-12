#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"

#include "FootIkAnimInstance.generated.h"

/** Numeric skinning data retained independently of the render buffers. */
struct FFootSoleInfluence
{
	FVector point;
	int32 bone;
	float weight;
};

struct FFootSolePoint
{
	TArray<FFootSoleInfluence, TInlineAllocator<4>> influences;
	int32 side;
};

/** Samples the clip as the stock single-node player does, then bends each leg onto the ground the game thread found under its foot. */
USTRUCT()
struct FFootIkAnimInstanceProxy : public FAnimSingleNodeInstanceProxy
{
	GENERATED_BODY()

	FFootIkAnimInstanceProxy() = default;
	explicit FFootIkAnimInstanceProxy(UAnimInstance* instance);

	virtual bool Evaluate(FPoseContext& output) override;

	/** Left then right: how far the ground under each foot lies above the plane the body stands on, and which way it faces, in component space. */
	float groundHeights[2] = { 0.0f, 0.0f };
	FVector groundNormals[2] = { FVector::UpVector, FVector::UpVector };

	/** How far the whole body rises above the clip, in component space. */
	float lift = 0.0f;
	bool plantFeet = false;
	float headScale = 1.0f;
	bool groundValid[2] = { false, false };
	FVector groundPoints[2] = { FVector::ZeroVector, FVector::ZeroVector };
	TArray<FFootSolePoint> solePoints;
	TWeakObjectPtr<USkeletalMesh> soleMesh;
	int32 soleLod = INDEX_NONE;
	TWeakObjectPtr<USkeletalMesh> unavailableSoleMesh;
};

/**
 * The stock single-node player, which also keeps both feet on uneven ground: each leg bends to the ground under its foot and the pelvis drops for the lower one.
 * Not grounded, the feet ease back to the clip, which is what airborne and swimming bodies want.
 */
UCLASS(Transient, NotBlueprintable)
class ALVERION_API UFootIkAnimInstance : public UAnimSingleNodeInstance
{
	GENERATED_BODY()

public:
	bool grounded = false;
	bool plantFeet = false;

	/** How far the whole body rises above the clip, in component space, for a body whose clips reach lower than its reference pose stands. */
	float lift = 0.0f;


protected:
	virtual void NativeUpdateAnimation(float deltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
