#include "Appearance/FootIkAnimInstance.h"

#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "TwoBoneIK.h"

namespace
{
	// How far above and below the body's feet a foot looks for ground.
	constexpr double footIkReachCm = 45.0;

	// The share of the step between the two feet's ground the pelvis sinks below the lower foot, so neither leg stands locked straight on a slope.
	constexpr float footIkSoftKnees = 0.3f;

	/** Invalid when the skeleton lacks the bone or the current level of detail drops it. */
	FCompactPoseBoneIndex footIkBone(const FBoneContainer& bones, const FName& name)
	{
		const int32 meshIndex = bones.GetPoseBoneIndexForBoneName(name);
		return (meshIndex == INDEX_NONE) ? FCompactPoseBoneIndex(INDEX_NONE) : bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(meshIndex));
	}

}

FFootIkAnimInstanceProxy::FFootIkAnimInstanceProxy(UAnimInstance* instance)
	: FAnimSingleNodeInstanceProxy(instance)
{
}

bool FFootIkAnimInstanceProxy::Evaluate(FPoseContext& output)
{
	FAnimSingleNodeInstanceProxy::Evaluate(output);

	if (!FMath::IsNearlyEqual(headScale, 1.0f))
	{
		static const FName headName(TEXT("head"));
		const FCompactPoseBoneIndex head = footIkBone(output.Pose.GetBoneContainer(), headName);
		if (head.IsValid())
		{
			FTransform& headTransform = output.Pose[head];
			headTransform.SetScale3D(headTransform.GetScale3D() * headScale);
		}
	}

	// Level ground leaves the clip as it was sampled.
	bool level = true;
	for (int32 side = 0; side < 2; side++)
	{
		level = level && FMath::IsNearlyZero(groundHeights[side], 0.1f) && groundNormals[side].Equals(FVector::UpVector, 0.001);
	}

	static const FName pelvisName(TEXT("pelvis"));
	static const FName legNames[2][4] = { { TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l") }, { TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r") } };
	const FBoneContainer& bones = output.Pose.GetBoneContainer();
	const FCompactPoseBoneIndex pelvis = footIkBone(bones, pelvisName);
	if ((!plantFeet && level && FMath::IsNearlyZero(lift)) || !pelvis.IsValid())
	{
		return true;
	}

	FCSPose<FCompactPose> pose;
	pose.InitPose(output.Pose);

	// The pelvis drops for the lower foot and a little more, so both knees soften and the higher one bends furthest.
	// The lift raises it with everything it carries, legs and feet included.
	float pelvisDrop = FMath::Min(groundHeights[0], groundHeights[1]) - (footIkSoftKnees * FMath::Abs(groundHeights[0] - groundHeights[1]));
	if (plantFeet && !solePoints.IsEmpty())
	{
		double clearance = MAX_dbl;
		bool complete = true;
		for (const FFootSolePoint& sole : solePoints)
		{
			if (!groundValid[sole.side])
			{
				continue;
			}

			FVector point = FVector::ZeroVector;
			for (const FFootSoleInfluence& influence : sole.influences)
			{
				const FCompactPoseBoneIndex bone = bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(influence.bone));
				if (!bone.IsValid())
				{
					complete = false;
					break;
				}

				point += pose.GetComponentSpaceTransform(bone).TransformPosition(influence.point) * influence.weight;
			}

			if (!complete)
			{
				break;
			}

			const FVector& normal = groundNormals[sole.side];
			clearance = FMath::Min(clearance, FVector::DotProduct(point - groundPoints[sole.side], normal) / FMath::Max(normal.Z, 0.5));
		}

		if (complete && (clearance != MAX_dbl))
		{
			pelvisDrop = static_cast<float>(-clearance) - lift;
		}
	}

	FTransform pelvisTransform = pose.GetComponentSpaceTransform(pelvis);
	pelvisTransform.AddToTranslation(FVector(0.0, 0.0, pelvisDrop + lift));
	const FBoneTransform pelvisMove[] = { FBoneTransform(pelvis, pelvisTransform) };
	pose.SafeSetCSBoneTransforms(pelvisMove);

	for (int32 side = 0; !plantFeet && !level && (side < 2); side++)
	{
		const FCompactPoseBoneIndex hipBone = footIkBone(bones, legNames[side][0]);
		const FCompactPoseBoneIndex kneeBone = footIkBone(bones, legNames[side][1]);
		const FCompactPoseBoneIndex footBone = footIkBone(bones, legNames[side][2]);
		const FCompactPoseBoneIndex ballBone = footIkBone(bones, legNames[side][3]);
		if (!hipBone.IsValid() || !kneeBone.IsValid() || !footBone.IsValid() || !ballBone.IsValid())
		{
			continue;
		}

		FTransform hip = pose.GetComponentSpaceTransform(hipBone);
		FTransform knee = pose.GetComponentSpaceTransform(kneeBone);
		FTransform foot = pose.GetComponentSpaceTransform(footBone);

		// The sole lies along the slope and the ankle keeps the clip's height above its ground measured across the slope, which straight up is that height over the slope's cosine.
		// The knee bends out over the toes: a standing leg is nearly straight, so the slight kink it already has could point anywhere, often sideways.
		const FVector reach = foot.GetLocation() + FVector(0.0, 0.0, groundHeights[side] - pelvisDrop + ((foot.GetLocation().Z - pelvisDrop) * ((1.0 / FMath::Max(groundNormals[side].Z, 0.5)) - 1.0)));
		AnimationCore::SolveTwoBoneIK(hip, knee, foot, knee.GetLocation() + ((pose.GetComponentSpaceTransform(ballBone).GetLocation() - foot.GetLocation()).GetSafeNormal2D() * 50.0), reach, false, 1.0, 1.0);
		foot.SetRotation(FQuat::FindBetweenNormals(FVector::UpVector, groundNormals[side]) * foot.GetRotation());
		const FBoneTransform leg[] = { FBoneTransform(hipBone, hip), FBoneTransform(kneeBone, knee), FBoneTransform(footBone, foot) };
		pose.SafeSetCSBoneTransforms(leg);
	}

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(pose), output.Pose);
	return true;
}

FAnimInstanceProxy* UFootIkAnimInstance::CreateAnimInstanceProxy()
{
	return new FFootIkAnimInstanceProxy(this);
}

void UFootIkAnimInstance::NativeUpdateAnimation(float deltaSeconds)
{
	Super::NativeUpdateAnimation(deltaSeconds);
	FFootIkAnimInstanceProxy& shaping = GetProxyOnGameThread<FFootIkAnimInstanceProxy>();
	shaping.lift = lift;
	shaping.headScale = 1.0f;

	const USkeletalMeshComponent* mesh = GetSkelMeshComponent();
	const UWorld* world = GetWorld();
	if ((mesh == nullptr) || (mesh->GetOwner() == nullptr) || (world == nullptr))
	{
		return;
	}

	static const FName orcTag(TEXT("AlverionOrc"));
	shaping.headScale = mesh->ComponentHasTag(orcTag) ? 0.95f : 1.0f;
	shaping.plantFeet = plantFeet;
	const USkeletalMesh* asset = mesh->GetSkeletalMeshAsset();
	const FSkeletalMeshRenderData* data = plantFeet ? mesh->GetSkeletalMeshRenderData() : nullptr;
	const int32 renderLod = (data != nullptr) && !data->LODRenderData.IsEmpty() ? FMath::Clamp(mesh->GetPredictedLODLevel(), 0, data->LODRenderData.Num() - 1) : INDEX_NONE;
	if (plantFeet && (asset != nullptr) && ((shaping.soleMesh.Get() != asset) || (shaping.soleLod != renderLod)))
	{
		shaping.soleMesh.Reset();
		shaping.soleLod = INDEX_NONE;
		shaping.solePoints.Reset();
		const FSkinWeightVertexBuffer* weights = renderLod != INDEX_NONE ? mesh->GetSkinWeightBuffer(renderLod) : nullptr;
		if ((data != nullptr) && data->LODRenderData.IsValidIndex(renderLod) && (weights != nullptr))
		{
			const FSkeletalMeshLODRenderData& lod = data->LODRenderData[renderLod];
			const FPositionVertexBuffer& vertices = lod.StaticVertexBuffers.PositionVertexBuffer;
			const bool lookupReady = !weights->GetVariableBonesPerVertex() || (weights->GetLookupVertexBuffer()->GetLookupData() != nullptr);
			if ((vertices.GetVertexData() != nullptr) && (weights->GetDataVertexBuffer()->GetWeightData() != nullptr) && lookupReady)
			{
				const FReferenceSkeleton& reference = asset->GetRefSkeleton();
				const TArray<FMatrix44f>& inverse = asset->GetRefBasesInvMatrix();
				const int32 left = reference.FindBoneIndex(TEXT("foot_l"));
				const int32 right = reference.FindBoneIndex(TEXT("foot_r"));
				if ((left != INDEX_NONE) && (right != INDEX_NONE))
				{
					const FVector leftFoot(inverse[left].Inverse().GetOrigin());
					const FVector rightFoot(inverse[right].Inverse().GetOrigin());
					const double ankleHeight = FMath::Max(leftFoot.Z, rightFoot.Z);
					for (const FSkelMeshRenderSection& section : lod.RenderSections)
					{
						for (uint32 vertex = section.BaseVertexIndex; vertex < section.BaseVertexIndex + section.NumVertices; vertex++)
						{
							const FVector3f point = vertices.VertexPosition(vertex);
							if (point.Z >= ankleHeight)
							{
								continue;
							}

							FFootSolePoint sole;
							sole.side = FVector::DistSquared(FVector(point), leftFoot) < FVector::DistSquared(FVector(point), rightFoot) ? 0 : 1;
							uint32 offset = 0;
							uint32 count = 0;
							weights->GetVertexInfluenceOffsetCount(vertex, offset, count);
							for (uint32 influence = 0; influence < count; influence++)
							{
								const uint16 weight = weights->GetBoneWeight(vertex, influence);
								if (weight != 0)
								{
									const int32 bone = section.BoneMap[weights->GetBoneIndex(vertex, influence)];
									sole.influences.Add({ FVector(inverse[bone].TransformPosition(point)), bone, static_cast<float>(weight) / 65535.0f });
								}
							}

							if (!sole.influences.IsEmpty())
							{
								shaping.solePoints.Add(MoveTemp(sole));
							}
						}
					}
				}
			}
		}

		if (!shaping.solePoints.IsEmpty())
		{
			shaping.soleMesh = const_cast<USkeletalMesh*>(asset);
			shaping.soleLod = renderLod;
		}
		else if (shaping.unavailableSoleMesh.Get() != asset)
		{
			shaping.unavailableSoleMesh = const_cast<USkeletalMesh*>(asset);
			UE_LOG(LogTemp, Warning, TEXT("Walk sole grounding has no CPU skin data for %s; retaining clip lift."), *asset->GetPathName());
		}
	}

	// Neither the body nor the character it rides on is ground.
	FCollisionQueryParams query(SCENE_QUERY_STAT(FootIk));
	query.AddIgnoredActor(mesh->GetOwner());
	query.AddIgnoredActor(mesh->GetOwner()->GetAttachParentActor());

	// Traced where each foot stood last frame; the height only matters under it, so last frame's place is close enough.
	// Applied at once, with no easing: the clip itself switches at once between standing and running, so the legs change with it.
	static const FName footNames[2] = { TEXT("foot_l"), TEXT("foot_r") };
	FFootIkAnimInstanceProxy& proxy = GetProxyOnGameThread<FFootIkAnimInstanceProxy>();
	const FTransform& frame = mesh->GetComponentTransform();
	for (int32 side = 0; side < 2; side++)
	{
		proxy.groundValid[side] = false;
		proxy.groundHeights[side] = 0.0f;
		proxy.groundNormals[side] = FVector::UpVector;
		proxy.groundPoints[side] = FVector::ZeroVector;
		const FVector foot = mesh->GetSocketLocation(footNames[side]);
		FHitResult hit;
		if ((grounded || plantFeet) && world->LineTraceSingleByChannel(hit, FVector(foot.X, foot.Y, frame.GetLocation().Z + footIkReachCm), FVector(foot.X, foot.Y, frame.GetLocation().Z - footIkReachCm), ECC_WorldStatic, query))
		{
			if (plantFeet)
			{
				proxy.groundValid[side] = true;
				proxy.groundPoints[side] = frame.InverseTransformPosition(hit.ImpactPoint);
				proxy.groundNormals[side] = (frame.InverseTransformVectorNoScale(hit.ImpactNormal) * frame.GetScale3D()).GetSafeNormal();
				continue;
			}

			proxy.groundHeights[side] = static_cast<float>(hit.ImpactPoint.Z - frame.GetLocation().Z);
			proxy.groundNormals[side] = frame.InverseTransformVectorNoScale(hit.ImpactNormal);
		}
	}
}
