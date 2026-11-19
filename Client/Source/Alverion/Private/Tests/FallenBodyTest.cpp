#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "World/ProxyCharacter.h"

namespace
{
	/** Stands a proxy of the kind given on the floor, makes it fall, plays the fall out and reports how its body lies against the floor. */
	void fallenBodyMeasure(FAutomationTestBase& test, UWorld& world, const AActor& terrain, bool monster)
	{
		AProxyCharacter* proxy = world.SpawnActor<AProxyCharacter>();
		Network::FCharacterSpawn spawn;
		spawn.isNpc = true;
		spawn.attackable = monster;
		spawn.location = FVector(monster ? 0.0 : 200.0, 0.0, 120.0);
		proxy->setup(spawn);

		// A MetaHuman body loads in the background and stands on a later frame, so the world runs until it does.
		TArray<AActor*> bodies;
		for (int32 tick = 0; (tick < 240) && bodies.IsEmpty(); tick++)
		{
			FlushAsyncLoading();
			world.Tick(LEVELTICK_All, 1.0f / 60.0f);
			proxy->GetAttachedActors(bodies);
		}

		proxy->Tick(1.0f);
		USkeletalMeshComponent* mesh = (bodies.Num() == 1) ? FindObjectFast<USkeletalMeshComponent>(bodies[0], TEXT("Body")) : nullptr;
		const TCHAR* kind = monster ? TEXT("goblin") : TEXT("townsperson");
		if (!test.TestNotNull(FString::Printf(TEXT("The %s stands a body"), kind), mesh))
		{
			return;
		}

		proxy->setFallen(true);
		const UAnimSingleNodeInstance* player = mesh->GetSingleNodeInstance();
		const UAnimSequenceBase* clip = (player != nullptr) ? Cast<UAnimSequenceBase>(player->GetAnimationAsset()) : nullptr;
		if (!test.TestNotNull(FString::Printf(TEXT("The %s's fall plays a clip"), kind), clip))
		{
			return;
		}

		// Every frame of the fall, then a second after it, so anything that eases in afterwards shows too.
		const FSkeletalMeshLODRenderData& lod = mesh->GetSkeletalMeshRenderData()->LODRenderData[0];
		const FSkinWeightVertexBuffer* weights = mesh->GetSkinWeightBuffer(0);
		const double floorTop = terrain.GetActorLocation().Z + 10.0;
		TArray<FMatrix44f> matrices;
		TArray<FVector3f> positions;
		float lowest = 0.0f;
		float highest = 0.0f;
		const int32 frames = FMath::CeilToInt(clip->GetPlayLength() * 30.0f) + 30;
		for (int32 frame = 0; frame <= frames; frame++)
		{
			proxy->Tick(1.0f / 30.0f);
			mesh->TickAnimation(1.0f / 30.0f, false);
			mesh->RefreshBoneTransforms();
			mesh->GetCurrentRefToLocalMatrices(matrices, 0);
			USkinnedMeshComponent::ComputeSkinnedPositions(mesh, positions, matrices, lod, *weights);
			const FTransform& transform = mesh->GetComponentTransform();
			lowest = MAX_flt;
			highest = -MAX_flt;
			for (const FVector3f& position : positions)
			{
				const float height = static_cast<float>(transform.TransformPosition(FVector(position)).Z - floorTop);
				lowest = FMath::Min(lowest, height);
				highest = FMath::Max(highest, height);
			}
		}

		test.AddInfo(FString::Printf(TEXT("%s on the %s: lowest %.1f cm, highest %.1f cm above the floor."), *clip->GetName(), kind, lowest, highest));
		test.TestTrue(FString::Printf(TEXT("The fallen %s lies on the floor rather than under it"), kind), lowest > -3.0f);
		test.TestTrue(FString::Printf(TEXT("The fallen %s lies on the floor rather than over it"), kind), lowest < 6.0f);
		test.TestTrue(FString::Printf(TEXT("The %s lies down rather than standing"), kind), highest < 70.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFallenBodyTest, "Alverion.Characters.FallenBody", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFallenBodyTest::RunTest(const FString& parameters)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(world);
	ON_SCOPE_EXIT
	{
		world->EndPlay(EEndPlayReason::EndPlayInEditor);
		world->DestroyWorld(false);
		GEngine->DestroyWorldContext(world);
	};

	if (!TestNotNull(TEXT("The assembled goblin is installed"), LoadClass<AActor>(nullptr, TEXT("/Game/Characters/MetaHumans/Goblins/Male/Goblin/BP_MH_Goblin.BP_MH_Goblin_C"))))
	{
		return false;
	}

	world->InitializeActorsForPlay(FURL());
	world->SetBegunPlay(true);

	// A flat floor whose top is at 120, which the monster settles on.
	AActor* terrain = world->SpawnActor<AActor>();
	UBoxComponent* floor = NewObject<UBoxComponent>(terrain);
	terrain->SetRootComponent(floor);
	floor->InitBoxExtent(FVector(500.0, 500.0, 10.0));
	floor->SetCollisionProfileName(TEXT("BlockAll"));
	floor->RegisterComponent();
	terrain->SetActorLocation(FVector(0.0, 0.0, 110.0));

	// The goblin, whose body stands at once. A townsperson's body loads in the background and never stands in a headless run.
	fallenBodyMeasure(*this, *world, *terrain, true);
	return true;
}

#endif
