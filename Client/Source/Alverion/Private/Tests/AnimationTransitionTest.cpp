#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSingleNodeInstance.h"
#include "Appearance/AppearanceApplier.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationTransitionTest, "Alverion.Characters.AnimationTransitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationTransitionTest::RunTest(const FString& parameters)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(world);
	ON_SCOPE_EXIT
	{
		world->DestroyWorld(false);
		GEngine->DestroyWorldContext(world);
	};

	AActor* body = world->SpawnActor<AActor>();
	USkeletalMeshComponent* mesh = NewObject<USkeletalMeshComponent>(body, TEXT("Body"));
	USkeletalMesh* asset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/MetaHumans/Humans/Defaults/Male/Body/SKM_MH_HumanMale_BodyMesh.SKM_MH_HumanMale_BodyMesh"));
	if (!TestNotNull(TEXT("The human body mesh is installed"), asset))
	{
		return false;
	}

	mesh->SetSkeletalMeshAsset(asset);
	body->SetRootComponent(mesh);
	mesh->RegisterComponent();
	Appearance::animate(*body, Appearance::EMotion::Idle, 0.0f);
	UAnimSingleNodeInstance* player = mesh->GetSingleNodeInstance();
	if (!TestNotNull(TEXT("The body plays an idle clip"), player) || !TestNotNull(TEXT("The idle clip is installed"), player->GetAnimationAsset()))
	{
		return false;
	}

	const FName hand(TEXT("hand_r"));
	const auto sampleHand = [mesh, &hand](float seconds) -> FVector
	{
		mesh->TickAnimation(seconds, false);
		mesh->RefreshBoneTransforms();
		return mesh->GetBoneLocation(hand, EBoneSpaces::ComponentSpace);
	};

	const auto checkTransition = [this, &sampleHand, player](const TCHAR* label, const FVector& source, float position)
	{
		player->SetPlaying(false);
		player->SetPosition(position, false);
		const FVector first = sampleHand(1.0f / 120.0f);
		const FVector middle = sampleHand(0.04f);
		const FVector target = sampleHand(0.25f);
		const double travel = FVector::Dist(source, target);
		TestTrue(FString::Printf(TEXT("%s has distinct poses"), label), travel > 1.0);
		TestTrue(FString::Printf(TEXT("%s starts near the last visible pose"), label), FVector::Dist(first, source) < travel * 0.2);
		TestTrue(FString::Printf(TEXT("%s passes through a blended pose"), label), (FVector::Dist(middle, source) > travel * 0.1) && (FVector::Dist(middle, target) > travel * 0.1));
		TestTrue(FString::Printf(TEXT("%s reaches the target pose"), label), FVector::Dist(target, sampleHand(1.0f / 120.0f)) < 0.1);
	};

	player->SetPlaying(false);
	FVector source = sampleHand(0.25f);
	Appearance::animate(*body, Appearance::EMotion::Swim, 0.0f);
	checkTransition(TEXT("Entering water"), source, 0.0f);
	source = sampleHand(1.0f / 120.0f);
	Appearance::animate(*body, Appearance::EMotion::Idle, 0.0f);
	checkTransition(TEXT("Leaving water"), source, 0.0f);

	Appearance::animate(*body, Appearance::EMotion::Swim, 0.0f);
	player->SetPlaying(false);
	player->SetPosition(0.0f, false);
	source = sampleHand(0.08f);
	Appearance::animate(*body, Appearance::EMotion::Idle, 0.0f);
	checkTransition(TEXT("Leaving water during the entry blend"), source, 0.0f);

	Appearance::armWithSword(*body);
	UStaticMeshComponent* weapon = FindObjectFast<UStaticMeshComponent>(body, TEXT("StarterWoodenSword"));
	if (!TestNotNull(TEXT("The starter sword is installed"), weapon))
	{
		return false;
	}

	const FVector hip = weapon->GetComponentLocation();
	Appearance::holdWeapon(*body, true);
	TestEqual(TEXT("The sword attaches to the hand"), weapon->GetAttachSocketName(), hand);
	TestTrue(TEXT("The sword starts its draw at the hip"), FVector::Dist(weapon->GetComponentLocation(), hip) < 0.1);
	world->Tick(LEVELTICK_All, 0.25f);
	const FVector grip = weapon->GetComponentLocation();
	TestTrue(TEXT("The sword moves away from its starting point"), FVector::Dist(grip, hip) > 1.0);
	Appearance::holdWeapon(*body, false);
	TestTrue(TEXT("The sword starts stowing from the hand"), FVector::Dist(weapon->GetComponentLocation(), grip) < 0.1);
	world->Tick(LEVELTICK_All, 0.25f);
	TestTrue(TEXT("The sword returns to the hip"), FVector::Dist(weapon->GetComponentLocation(), hip) < 0.1);

	source = sampleHand(0.25f);
	Appearance::animate(*body, Appearance::EMotion::CombatIdle, 0.0f);
	checkTransition(TEXT("Taking the sword stance"), source, 0.0f);
	source = sampleHand(1.0f / 120.0f);
	Appearance::perform(*body, Appearance::EAction::Strike, 0);
	checkTransition(TEXT("Starting a sword attack"), source, 0.25f);
	source = sampleHand(1.0f / 120.0f);
	Appearance::perform(*body, Appearance::EAction::Strike, 0);
	TestEqual(TEXT("The same attack restarts at frame zero"), player->GetCurrentTime(), 0.0f);
	checkTransition(TEXT("Restarting the same sword attack"), source, 0.0f);

	for (int32 frame = 0; frame < 120; frame++)
	{
		world->Tick(LEVELTICK_All, 1.0f / 60.0f);
	}
	source = sampleHand(1.0f / 120.0f);
	Appearance::animate(*body, Appearance::EMotion::Idle, 0.0f);
	checkTransition(TEXT("Recovering from a sword attack"), source, 0.0f);

	return true;
}

#endif
