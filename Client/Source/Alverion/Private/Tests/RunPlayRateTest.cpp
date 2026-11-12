#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimationAsset.h"
#include "Appearance/AppearanceApplier.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"

namespace
{
	// Measures a clip on a body the way the game does, and says what it got.
	float runTestSpeedOf(const TCHAR* clipPath, const TCHAR* meshPath)
	{
		const UAnimationAsset* clip = LoadObject<UAnimationAsset>(nullptr, clipPath);
		const USkeletalMesh* mesh = LoadObject<USkeletalMesh>(nullptr, meshPath);
		return ((clip != nullptr) && (mesh != nullptr)) ? Appearance::measureRunSpeed(*clip, *mesh) : 0.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunPlayRateTest, "Alverion.Characters.RunPlayRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRunPlayRateTest::RunTest(const FString& parameters)
{
	// The clips as measured in the editor when this was written: the planted foot's slide on each body, within a few percent.
	// The jog gives 573 on the men's bodies and 537 on the women's, and the goblin's clip 434 on its short legs.
	const TCHAR* const jog = TEXT("/Game/Characters/Animations/UAL/Jog_Fwd_Loop.Jog_Fwd_Loop");
	const TCHAR* const goblinJog = TEXT("/Game/Characters/Animations/UAL/Goblin/Jog_Fwd_Loop_Goblin.Jog_Fwd_Loop_Goblin");
	const float man = runTestSpeedOf(jog, TEXT("/Game/Characters/MetaHumans/Humans/Male/Victor/Body/SKM_MH_Victor_BodyMesh.SKM_MH_Victor_BodyMesh"));
	const float woman = runTestSpeedOf(jog, TEXT("/Game/Characters/MetaHumans/Humans/Female/Ada/Body/SKM_MH_Ada_BodyMesh.SKM_MH_Ada_BodyMesh"));
	const float goblin = runTestSpeedOf(goblinJog, TEXT("/Game/Characters/MetaHumans/Goblins/Male/Goblin/Body/SKM_MHC_Goblin_BodyMesh.SKM_MHC_Goblin_BodyMesh"));
	AddInfo(FString::Printf(TEXT("Measured run speeds: man %.0f, woman %.0f, goblin %.0f cm/s."), man, woman, goblin));
	TestTrue(TEXT("The jog on a man's body"), FMath::IsNearlyEqual(man, 573.0f, 573.0f * 0.05f));
	TestTrue(TEXT("The jog on a woman's body"), FMath::IsNearlyEqual(woman, 537.0f, 537.0f * 0.05f));
	TestTrue(TEXT("The goblin's jog"), FMath::IsNearlyEqual(goblin, 434.0f, 434.0f * 0.05f));

	// A clip that is not a sequence, or a missing one, measures nothing.
	TestEqual(TEXT("Nothing to measure"), runTestSpeedOf(TEXT("/Game/Missing.Missing"), TEXT("/Game/Missing.Missing")), 0.0f);

	// The clip follows the ground speed, not the gait: the one that leaves the feet the least slip once its rate is clamped to 0.5 to 1.5.
	// A walk at 95 covers 47.5 to 142.5 and a jog at 555 covers 277.5 to 832.5, so between them the walk slips by speed over 142.5 and the jog by 277.5 over speed.
	// Those are equal at the geometric mean, 198.9; a walker at 230 slips 61% on the walk and 21% on the jog.
	const Appearance::FMovingClip human[] = { { Appearance::EMotion::Walk, 95.0f }, { Appearance::EMotion::Run, 555.0f } };
	const Appearance::EMotion fresh = Appearance::EMotion::Idle;
	TestTrue(TEXT("A slow walker plays the walk"), Appearance::pickMovingMotion(human, 100.0f, fresh) == Appearance::EMotion::Walk);
	TestTrue(TEXT("A fast walker plays the jog"), Appearance::pickMovingMotion(human, 230.0f, fresh) == Appearance::EMotion::Run);
	TestTrue(TEXT("A runner plays the jog"), Appearance::pickMovingMotion(human, 460.0f, fresh) == Appearance::EMotion::Run);
	TestTrue(TEXT("Not moving plays the walk"), Appearance::pickMovingMotion(human, 0.0f, fresh) == Appearance::EMotion::Walk);
	TestTrue(TEXT("Just under the crossover plays the walk"), Appearance::pickMovingMotion(human, 195.0f, fresh) == Appearance::EMotion::Walk);
	TestTrue(TEXT("Just over the crossover plays the jog"), Appearance::pickMovingMotion(human, 203.0f, fresh) == Appearance::EMotion::Run);

	// Near the crossover the clip already playing stays, and far from it the other clip takes over.
	TestTrue(TEXT("A walk stays through the crossover"), Appearance::pickMovingMotion(human, 202.0f, Appearance::EMotion::Walk) == Appearance::EMotion::Walk);
	TestTrue(TEXT("A jog stays through the crossover"), Appearance::pickMovingMotion(human, 195.0f, Appearance::EMotion::Run) == Appearance::EMotion::Run);
	TestTrue(TEXT("A walk gives way to the jog once it slips much more"), Appearance::pickMovingMotion(human, 300.0f, Appearance::EMotion::Walk) == Appearance::EMotion::Run);
	TestTrue(TEXT("A jog gives way to the walk once it slips much more"), Appearance::pickMovingMotion(human, 100.0f, Appearance::EMotion::Run) == Appearance::EMotion::Walk);

	// The goblin has one clip, whatever its speed.
	const Appearance::FMovingClip goblinClips[] = { { Appearance::EMotion::GoblinRun, 434.0f } };
	TestTrue(TEXT("A slow goblin plays its run"), Appearance::pickMovingMotion(goblinClips, 100.0f, fresh) == Appearance::EMotion::GoblinRun);
	TestTrue(TEXT("A fast goblin plays its run"), Appearance::pickMovingMotion(goblinClips, 460.0f, Appearance::EMotion::GoblinRun) == Appearance::EMotion::GoblinRun);

	// A clip that could not be measured is not considered, and when none could be, the last one is used.
	const Appearance::FMovingClip unmeasured[] = { { Appearance::EMotion::Walk, 95.0f }, { Appearance::EMotion::Run, 0.0f } };
	TestTrue(TEXT("An unmeasured clip is passed over"), Appearance::pickMovingMotion(unmeasured, 460.0f, fresh) == Appearance::EMotion::Walk);
	const Appearance::FMovingClip none[] = { { Appearance::EMotion::Walk, 0.0f }, { Appearance::EMotion::Run, 0.0f } };
	TestTrue(TEXT("With none measured the last is used"), Appearance::pickMovingMotion(none, 100.0f, fresh) == Appearance::EMotion::Run);

	// The walk clip measures on a body like the run clips, and is slower.
	const float walk = runTestSpeedOf(TEXT("/Game/Characters/Animations/UAL/Walk_Loop.Walk_Loop"), TEXT("/Game/Characters/MetaHumans/Humans/Male/Victor/Body/SKM_MH_Victor_BodyMesh.SKM_MH_Victor_BodyMesh"));
	AddInfo(FString::Printf(TEXT("Measured walk speed: man %.0f cm/s."), walk));
	TestTrue(TEXT("The walk clip travels, slower than the jog"), (walk > 50.0f) && (walk < man));

	// The rate follows speed over the clip's, kept in range, on made-up speeds.
	TestEqual(TEXT("A body at the clip's own speed plays it at its own rate"), Appearance::playRate(500.0f, 500.0f), 1.0f);
	TestEqual(TEXT("A slower body plays the clip slower"), Appearance::playRate(400.0f, 500.0f), 0.8f);
	TestEqual(TEXT("A faster body plays it faster"), Appearance::playRate(600.0f, 500.0f), 1.2f);
	TestEqual(TEXT("A body barely moving does not crawl"), Appearance::playRate(0.0f, 500.0f), 0.5f);
	TestEqual(TEXT("A teleport does not blur"), Appearance::playRate(100000.0f, 500.0f), 1.5f);
	TestEqual(TEXT("A clip that could not be measured plays at its own rate"), Appearance::playRate(460.0f, 0.0f), 1.0f);
	return true;
}

#endif
