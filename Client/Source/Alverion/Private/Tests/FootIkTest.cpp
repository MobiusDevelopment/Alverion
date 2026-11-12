#if WITH_DEV_AUTOMATION_TESTS

#include "Appearance.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "World/ProxyCharacter.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FFootIkTest, "Alverion.Characters.FootIk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FFootIkTest::GetTests(TArray<FString>& names, TArray<FString>& commands) const
{
	// A townsperson wears the default man, and a player who picked nothing wears the default woman.
	names.Add(TEXT("Townsperson"));
	commands.Add(TEXT("Victor"));
	names.Add(TEXT("Player"));
	commands.Add(TEXT("Jelena"));
}

bool FFootIkTest::RunTest(const FString& parameters)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(world);
	ON_SCOPE_EXIT
	{
		world->EndPlay(EEndPlayReason::EndPlayInEditor);
		world->DestroyWorld(false);
		GEngine->DestroyWorldContext(world);
	};

	UClass* character = LoadClass<AActor>(nullptr, *FString::Printf(TEXT("/Game/Characters/MetaHumans/Humans/%s/%s/BP_MH_%s.BP_MH_%s_C"), parameters == TEXT("Victor") ? TEXT("Male") : TEXT("Female"), *parameters, *parameters, *parameters));
	if (!TestNotNull(TEXT("The character is installed"), character))
	{
		return false;
	}

	world->InitializeActorsForPlay(FURL());
	world->SetBegunPlay(true);
	AActor* ground = world->SpawnActor<AActor>();
	UBoxComponent* slab = NewObject<UBoxComponent>(ground);
	ground->SetRootComponent(slab);
	slab->InitBoxExtent(FVector(500.0, 500.0, 10.0));
	slab->SetCollisionProfileName(TEXT("BlockAll"));
	slab->RegisterComponent();

	AProxyCharacter* proxy = world->SpawnActor<AProxyCharacter>();
	Network::FCharacterSpawn spawn;
	spawn.isNpc = (parameters == TEXT("Victor"));
	spawn.appearance = Network::FAppearance().pack();
	spawn.location = FVector(0.0, 0.0, 10.0);
	proxy->setup(spawn);

	// Bodies stand on the frame after they are asked for.
	world->Tick(LEVELTICK_All, 1.0f / 60.0f);
	TArray<AActor*> bodies;
	proxy->GetAttachedActors(bodies);
	if (!TestEqual(TEXT("The character has one body"), bodies.Num(), 1))
	{
		return false;
	}

	USkeletalMeshComponent* mesh = FindObjectFast<USkeletalMeshComponent>(bodies[0], TEXT("Body"));
	if (!TestNotNull(TEXT("The character has a body mesh"), mesh))
	{
		return false;
	}

	// Every level of detail keeps the bones the legs bend with, or the bend quietly stops at that distance.
	static const FName legBones[] = { TEXT("pelvis"), TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l"), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r") };
	const TIndirectArray<FSkeletalMeshLODRenderData>& levels = mesh->GetSkeletalMeshRenderData()->LODRenderData;
	for (int32 level = 0; level < levels.Num(); level++)
	{
		for (const FName& bone : legBones)
		{
			const bool kept = levels[level].RequiredBones.Contains(static_cast<FBoneIndexType>(mesh->GetBoneIndex(bone)));
			if (!kept)
			{
				AddInfo(FString::Printf(TEXT("Level of detail %d drops %s."), level, *bone.ToString()));
			}

			TestTrue(TEXT("Every level of detail keeps the leg bones"), kept);
		}
	}

	proxy->Tick(1.0f / 60.0f);
	UAnimSingleNodeInstance* player = mesh->GetSingleNodeInstance();
	const UAnimSequenceBase* idle = (player != nullptr) ? Cast<UAnimSequenceBase>(player->GetAnimationAsset()) : nullptr;
	if (!TestNotNull(TEXT("The character plays its idle"), idle))
	{
		return false;
	}

	// Each sole's lowest skinned point above the slab, at moments through the idle, facing up, across and down a slope.
	// On flat ground it is how the clip meets the floor; on the slope it should be no deeper.
	static const FName feet[2] = { TEXT("foot_l"), TEXT("foot_r") };
	const FSkeletalMeshLODRenderData& lod = mesh->GetSkeletalMeshRenderData()->LODRenderData[0];
	const FSkinWeightVertexBuffer* weights = mesh->GetSkinWeightBuffer(0);
	TArray<FMatrix44f> matrices;
	TArray<FVector3f> positions;
	player->SetPlaying(false);
	constexpr int32 moments = 8;
	double flatClearance[moments][2];
	for (const double incline : { 0.0, 35.0 })
	{
		ground->SetActorRotation(FRotator(0.0, 0.0, incline));
		for (const float yaw : { 0.0f, 90.0f, 180.0f, 270.0f })
		{
			proxy->standAt(spawn.location, yaw);
			for (int32 moment = 0; moment < moments; moment++)
			{
				// Held at one moment while the feet settle on what is under them.
				mesh->SetPosition(idle->GetPlayLength() * moment / moments, false);
				for (int32 frame = 0; frame < 60; frame++)
				{
					mesh->TickAnimation(1.0f / 60.0f, false);
					mesh->RefreshBoneTransforms();
				}

				const FTransform& transform = mesh->GetComponentTransform();
				const FVector3f normal(transform.InverseTransformVectorNoScale(ground->GetActorUpVector()));
				const FVector3f surface(transform.InverseTransformPosition(ground->GetActorLocation() + (ground->GetActorUpVector() * 10.0)));
				const FVector3f footPlaces[2] = { FVector3f(mesh->GetBoneLocation(feet[0], EBoneSpaces::ComponentSpace)), FVector3f(mesh->GetBoneLocation(feet[1], EBoneSpaces::ComponentSpace)) };
				mesh->GetCurrentRefToLocalMatrices(matrices, 0);
				USkinnedMeshComponent::ComputeSkinnedPositions(mesh, positions, matrices, lod, *weights);
				float lowest[2] = { MAX_flt, MAX_flt };
				for (const FVector3f& position : positions)
				{
					const int32 side = (FVector3f::DistSquared(position, footPlaces[0]) < FVector3f::DistSquared(position, footPlaces[1])) ? 0 : 1;
					if (FVector3f::DistSquared(position, footPlaces[side]) < 25.0f * 25.0f)
					{
						lowest[side] = FMath::Min(lowest[side], FVector3f::DotProduct(position - surface, normal));
					}
				}

				for (int32 side = 0; side < 2; side++)
				{
					// A bent knee points over its toes, never out to the side.
					static const FName legs[2][3] = { { TEXT("thigh_l"), TEXT("calf_l"), TEXT("ball_l") }, { TEXT("thigh_r"), TEXT("calf_r"), TEXT("ball_r") } };
					const FVector hip = mesh->GetBoneLocation(legs[side][0], EBoneSpaces::ComponentSpace);
					const FVector knee = mesh->GetBoneLocation(legs[side][1], EBoneSpaces::ComponentSpace);
					const FVector ankle(footPlaces[side]);
					const FVector bend = knee - FMath::ClosestPointOnLine(hip, ankle, knee);
					const FVector toes = (mesh->GetBoneLocation(legs[side][2], EBoneSpaces::ComponentSpace) - ankle).GetSafeNormal2D();
					if (bend.Size() > 3.0)
					{
						AddInfo(FString::Printf(TEXT("%s, %.0f degrees, facing %.0f, moment %d: knee bent %.1f cm, %.2f along the toes."), *legs[side][1].ToString(), incline, yaw, moment, bend.Size(), FVector::DotProduct(bend.GetSafeNormal(), toes)));
						TestTrue(TEXT("A bent knee points over its toes"), FVector::DotProduct(bend.GetSafeNormal(), toes) > 0.7);
					}

					if ((incline == 0.0) && (yaw == 0.0f))
					{
						flatClearance[moment][side] = lowest[side];
					}
					else if (incline != 0.0)
					{
						AddInfo(FString::Printf(TEXT("%s, %.0f degrees, facing %.0f, moment %d: sole %.2f cm above the slab, %.2f on flat."), *feet[side].ToString(), incline, yaw, moment, lowest[side], flatClearance[moment][side]));
					}

					TestTrue(TEXT("A sole sinks no deeper on a slope than on flat ground"), lowest[side] > flatClearance[moment][side] - 1.0);
				}
			}
		}
	}

	return true;
}

#endif
