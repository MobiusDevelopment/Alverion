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

	/** Fallen, which keeps the pelvis of a body lying down from sinking below the ground it lies on. */
	bool fallen = false;
	bool groundValid[2] = { false, false };
	FVector groundPoints[2] = { FVector::ZeroVector, FVector::ZeroVector };
	TArray<FFootSolePoint> solePoints;
	TWeakObjectPtr<USkeletalMesh> soleMesh;
	int32 soleLod = INDEX_NONE;
	TWeakObjectPtr<USkeletalMesh> unavailableSoleMesh;

	/** A one-off playing from the spine up while the legs keep the clip below, how far into it, and how much of it shows. */
	const UAnimSequenceBase* upperBodyClip = nullptr;
	float upperBodyTime = 0.0f;
	float upperBodyWeight = 0.0f;

private:
	friend class UFootIkAnimInstance;

	void blendPose(FPoseContext& output) const;

	/** Lays the one-off over every bone from the first spine bone up. */
	void layerUpperBody(FPoseContext& output) const;

	TArray<FTransform> _blendSource;
	float _blendDuration = 0.0f;
	float _blendElapsed = 0.0f;
	bool _hasEvaluatedPose = false;
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
	virtual void SetAnimationAsset(UAnimationAsset* asset, bool looping = true, float playRate = 1.0f) override;

	bool grounded = false;
	bool plantFeet = false;

	/** How far the whole body rises above the clip, in component space, for a body whose clips reach lower than its reference pose stands. */
	float lift = 0.0f;

	/** When the one-off playing over the body's motions ends, in world seconds. The motions wait until then. */
	double actionEndSeconds = 0.0;
	bool strikeAction = false;

	/** Fallen, which holds the fall's last pose until the body gets up. */
	bool fallen = false;

	/** Moving over land, set by the body's motion each frame, which is when a strike plays from the spine up. */
	bool moving = false;

	/** When the one-off playing from the spine up ends, in world seconds. */
	double upperBodyEndSeconds = 0.0;

	/** Plays a one-off from the spine up, this far into it, while the clip playing goes on below the waist. */
	void playUpperBody(UAnimSequenceBase* clip, float startSeconds);

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float deltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	UPROPERTY()
	TObjectPtr<UAnimSequenceBase> _upperBodyClip;

	float _upperBodyTime = 0.0f;
};
