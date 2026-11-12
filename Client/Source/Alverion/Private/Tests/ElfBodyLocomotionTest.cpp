#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSingleNodeInstance.h"
#include "Appearance.h"
#include "Appearance/AppearanceApplier.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElfBodyLocomotionTest, "Alverion.Characters.ElfBodyLocomotion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElfBodyLocomotionTest::RunTest(const FString& parameters)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(world);
	ON_SCOPE_EXIT
	{
		world->EndPlay(EEndPlayReason::EndPlayInEditor);
		world->DestroyWorld(false);
		GEngine->DestroyWorldContext(world);
	};

	world->InitializeActorsForPlay(FURL());
	world->SetBegunPlay(true);
	const TCHAR* const donors[2][4] = { { TEXT("Jelena"), TEXT("Vivian"), TEXT("Aera"), TEXT("Ada") }, { TEXT("Victor"), TEXT("Orlando"), TEXT("Kelvin"), TEXT("Cameron") } };
	const TCHAR* const suffixes[] = { TEXT(""), TEXT("B"), TEXT("C"), TEXT("D") };
	for (const bool male : { false, true })
	{
		for (uint8 head = 0; head < UE_ARRAY_COUNT(suffixes); ++head)
		{
			const FString name = FString(male ? TEXT("MH_ElfMale") : TEXT("MH_ElfFemale")) + suffixes[head];
			const FString path = FString::Printf(TEXT("/Game/Characters/MetaHumans/Elves/%s/%s/BP_%s.BP_%s_C"), male ? TEXT("Male") : TEXT("Female"), donors[male ? 1 : 0][head], *name, *name);
			UClass* blueprint = LoadClass<AActor>(nullptr, *path);
			if (!TestNotNull(*name, blueprint))
			{
				return false;
			}

			AActor* player = world->SpawnActor<AActor>(blueprint);
			Network::FAppearance look;
			look.species = Network::EAppearanceSpecies::Elf;
			look.male = male;
			look.head = head;
			Appearance::apply(*player, look);
			USkeletalMeshComponent* body = FindObjectFast<USkeletalMeshComponent>(player, TEXT("Body"));
			USkeletalMeshComponent* face = FindObjectFast<USkeletalMeshComponent>(player, TEXT("Face"));
			if (!TestNotNull(TEXT("The player has a body"), body) || !TestNotNull(TEXT("The player has a face"), face) || !TestNotNull(TEXT("The body has a mesh"), body->GetSkeletalMeshAsset()))
			{
				return false;
			}

			TestEqual(*name, body->GetSkeletalMeshAsset()->GetName(), FString::Printf(TEXT("SKM_%s_BodyMesh"), *name));
			TestTrue(TEXT("The player body is visible"), body->IsVisible());
			TArray<USkeletalMeshComponent*> meshes;
			player->GetComponents<USkeletalMeshComponent>(meshes);
			for (const USkeletalMeshComponent* mesh : meshes)
			{
				if ((mesh != body) && (mesh != face))
				{
					TestFalse(TEXT("The creator hides the preset clothing"), mesh->IsVisible());
				}
			}

			Appearance::animate(*player, Appearance::EMotion::Run, 0.0f);
			UAnimSingleNodeInstance* animation = body->GetSingleNodeInstance();
			if (!TestNotNull(TEXT("The body plays a locomotion animation"), animation))
			{
				return false;
			}

			double largestNeckGap = 0.0;
			double highestFoot = -MAX_dbl;
			double lowestFoot = MAX_dbl;
			for (int32 frame = 0; frame <= 12; ++frame)
			{
				body->SetPosition(animation->GetLength() * frame / 12.0f, false);
				body->TickAnimation(0.0f, false);
				body->RefreshBoneTransforms();
				face->TickAnimation(0.0f, false);
				face->RefreshBoneTransforms();
				for (const FName bone : { FName(TEXT("neck_01")), FName(TEXT("head")) })
				{
					largestNeckGap = FMath::Max(largestNeckGap, FVector::Distance(body->GetSocketLocation(bone), face->GetSocketLocation(bone)));
				}

				const double foot = body->GetSocketLocation(TEXT("foot_l")).Z;
				highestFoot = FMath::Max(highestFoot, foot);
				lowestFoot = FMath::Min(lowestFoot, foot);
			}

			AddInfo(FString::Printf(TEXT("%s neck/head gap %.3f cm, foot excursion %.2f cm."), *name, largestNeckGap, highestFoot - lowestFoot));
			TestTrue(TEXT("The neck and head stay aligned during locomotion"), largestNeckGap < 0.1);
			TestTrue(TEXT("The running foot lifts"), (highestFoot - lowestFoot) > 4.0);
			player->Destroy();
		}
	}

	return true;
}

#endif
