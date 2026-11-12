#if WITH_DEV_AUTOMATION_TESTS

#include "Appearance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "World/ProxyCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnQueueTest, "Alverion.Characters.SpawnQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	constexpr int32 spawnQueueCrowd = 3;

	int32 spawnQueueBodies(const TArray<AProxyCharacter*>& crowd)
	{
		int32 bodies = 0;
		for (AProxyCharacter* proxy : crowd)
		{
			TArray<AActor*> attached;
			proxy->GetAttachedActors(attached);
			bodies += attached.Num();
		}

		return bodies;
	}
}

bool FSpawnQueueTest::RunTest(const FString& parameters)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(world);
	ON_SCOPE_EXIT
	{
		world->EndPlay(EEndPlayReason::EndPlayInEditor);
		world->DestroyWorld(false);
		GEngine->DestroyWorldContext(world);
	};

	// Held in memory first, so the bodies wait on the queue alone and not on a fetch.
	UClass* townsperson = LoadClass<AActor>(nullptr, TEXT("/Game/Characters/MetaHumans/Humans/Male/Victor/BP_MH_Victor.BP_MH_Victor_C"));
	if (!TestNotNull(TEXT("The townsperson is installed"), townsperson))
	{
		return false;
	}

	world->InitializeActorsForPlay(FURL());
	world->SetBegunPlay(true);

	// A crowd the server introduces in one burst.
	TArray<AProxyCharacter*> crowd;
	for (int32 index = 0; index < spawnQueueCrowd; index++)
	{
		AProxyCharacter* proxy = world->SpawnActor<AProxyCharacter>();
		Network::FCharacterSpawn spawn;
		spawn.isNpc = true;
		spawn.appearance = Network::FAppearance().pack();
		spawn.location = FVector(index * 150.0, 0.0, 0.0);
		proxy->setup(spawn);
		crowd.Add(proxy);
	}

	TestEqual(TEXT("Nobody stands in the call that asked"), spawnQueueBodies(crowd), 0);
	for (int32 frame = 1; frame <= spawnQueueCrowd; frame++)
	{
		// The timer manager ticks once per engine frame, and a test's frames do not advance on their own.
		GFrameCounter++;
		world->Tick(LEVELTICK_All, 1.0f / 60.0f);
		TestEqual(TEXT("One body stands per frame"), spawnQueueBodies(crowd), frame);
	}

	return true;
}

#endif
