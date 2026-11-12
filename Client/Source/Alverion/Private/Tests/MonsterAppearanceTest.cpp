#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMonsterAppearanceTest, "Alverion.Characters.MonsterAppearance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMonsterAppearanceTest::RunTest(const FString& parameters)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(world);
	ON_SCOPE_EXIT
	{
		world->EndPlay(EEndPlayReason::EndPlayInEditor);
		world->DestroyWorld(false);
		GEngine->DestroyWorldContext(world);
	};

	UClass* goblin = LoadClass<AActor>(nullptr, TEXT("/Game/Characters/MetaHumans/Goblins/Male/Goblin/BP_MH_Goblin.BP_MH_Goblin_C"));
	if (!TestNotNull(TEXT("The assembled goblin is installed"), goblin))
	{
		return false;
	}

	world->InitializeActorsForPlay(FURL());
	world->SetBegunPlay(true);
	AProxyCharacter* proxy = world->SpawnActor<AProxyCharacter>();
	Network::FCharacterSpawn spawn;
	spawn.isNpc = true;
	spawn.attackable = true;
	spawn.runSpeed = 160.0f;
	proxy->setup(spawn);

	// Bodies stand on the frame after they are asked for.
	world->Tick(LEVELTICK_All, 1.0f / 60.0f);
	TArray<AActor*> bodies;
	proxy->GetAttachedActors(bodies);
	if (!TestEqual(TEXT("A monster has one body"), bodies.Num(), 1))
	{
		return false;
	}

	AActor* body = bodies[0];
	TestEqual(TEXT("Attackable NPCs wear the authored goblin"), body->GetClass(), goblin);
	proxy->setup(spawn);
	proxy->GetAttachedActors(bodies);
	TestTrue(TEXT("A repeated spawn keeps the same goblin"), (bodies.Num() == 1) && (bodies[0] == body));
	// World partition can supply terrain collision after the stationary monster has spawned.
	AActor* terrain = world->SpawnActor<AActor>();
	UBoxComponent* floor = NewObject<UBoxComponent>(terrain);
	terrain->SetRootComponent(floor);
	floor->InitBoxExtent(FVector(500.0, 500.0, 10.0));
	floor->SetCollisionProfileName(TEXT("BlockAll"));
	floor->RegisterComponent();
	terrain->SetActorLocation(FVector(0.0, 0.0, 110.0));
	proxy->Tick(1.0f);
	TestEqual(TEXT("The stationary monster settles on streamed terrain"), proxy->GetActorLocation().Z, 120.0, 0.1);

	USkeletalMeshComponent* mesh = FindObjectFast<USkeletalMeshComponent>(body, TEXT("Body"));
	if (!TestNotNull(TEXT("The monster has a body mesh"), mesh))
	{
		return false;
	}

	USkeletalMeshComponent* face = FindObjectFast<USkeletalMeshComponent>(body, TEXT("Face"));
	if (!TestNotNull(TEXT("The monster has a face mesh"), face))
	{
		return false;
	}

	const FName neckBone(TEXT("neck_01"));
	const FName headBone(TEXT("head"));
	for (const FName bone : { neckBone, headBone })
	{
		TestTrue(TEXT("Face and body both have the shared bone"), (mesh->GetBoneIndex(bone) != INDEX_NONE) && (face->GetBoneIndex(bone) != INDEX_NONE));
	}

	const FSkeletalMeshLODRenderData& lod = mesh->GetSkeletalMeshRenderData()->LODRenderData[0];
	const FSkinWeightVertexBuffer* weights = mesh->GetSkinWeightBuffer(0);
	for (const double incline : { 0.0, 10.0 })
	{
		terrain->SetActorRotation(FRotator(0.0, 0.0, incline));
		proxy->standAt(FVector(0.0, 0.0, 120.0), 0.0f);
		for (const bool running : { false, true })
		{
			if (running)
			{
				proxy->walkTo(FVector(300.0, 0.0, 120.0));
			}

			proxy->Tick(1.0f / 60.0f);
			const UAnimSingleNodeInstance* player = mesh->GetSingleNodeInstance();
			if (!TestNotNull(TEXT("The monster plays a locomotion clip"), player))
			{
				return false;
			}

			const UAnimSequence* clip = Cast<UAnimSequence>(player->GetAnimationAsset());
			if (!TestNotNull(TEXT("The monster has an animation sequence"), clip))
			{
				return false;
			}

			const FTransform& transform = mesh->GetComponentTransform();
			const FVector3f floorNormal(transform.InverseTransformVectorNoScale(terrain->GetActorUpVector()));
			const FVector3f floorPoint(transform.InverseTransformPosition(terrain->GetActorLocation() + terrain->GetActorUpVector() * 10.0));
			float lowest = MAX_flt;
			float highest = -MAX_flt;
			double largestSeparation = 0.0;
			TArray<FMatrix44f> matrices;
			TArray<FVector3f> positions;
			const int32 samples = FMath::CeilToInt(clip->GetPlayLength() * 120.0f);
			for (int32 frame = 0; frame <= samples; frame++)
			{
				mesh->SetPosition(clip->GetPlayLength() * frame / samples, false);
				mesh->TickAnimation(0.0f, false);
				mesh->RefreshBoneTransforms();
				face->TickAnimation(0.0f, false);
				face->RefreshBoneTransforms();
				for (const FName bone : { neckBone, headBone })
				{
					largestSeparation = FMath::Max(largestSeparation, FVector::Distance(mesh->GetSocketLocation(bone), face->GetSocketLocation(bone)));
				}

				mesh->GetCurrentRefToLocalMatrices(matrices, 0);
				USkinnedMeshComponent::ComputeSkinnedPositions(mesh, positions, matrices, lod, *weights);
				float sole = MAX_flt;
				for (const FVector3f& position : positions)
				{
					sole = FMath::Min(sole, FVector3f::DotProduct(position - floorPoint, floorNormal));
				}

				lowest = FMath::Min(lowest, sole);
				highest = FMath::Max(highest, sole);
			}

			AddInfo(FString::Printf(TEXT("%s on %.0f degrees: sole clearance %.2f to %.2f cm, shared bone separation %.3f cm."), *clip->GetName(), incline, lowest, highest, largestSeparation));
			TestTrue(TEXT("The neck and head follow the body throughout the clip"), largestSeparation < 0.1);
			TestTrue(TEXT("Planted feet meet the floor within half a centimetre"), FMath::Abs(lowest) < 0.5f);
			if (running)
			{
				TestTrue(TEXT("The jog keeps its airborne phase"), highest > 10.0f);
			}
		}
	}

	return true;
}

#endif
