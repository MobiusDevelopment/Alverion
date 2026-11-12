#if WITH_DEV_AUTOMATION_TESTS

#include "Appearance/AppearanceApplier.h"
#include "Appearance/AppearanceChoices.h"
#include "Components/PanelWidget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ScopeExit.h"
#include "FrontEnd/CharacterCreateWidget.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElfPlayerCreationTest, "Alverion.Characters.ElfPlayerCreation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElfPlayerCreationTest::RunTest(const FString& parameters)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(world);
	ON_SCOPE_EXIT
	{
		world->DestroyWorld(false);
		GEngine->DestroyWorldContext(world);
	};

	UCharacterCreateWidget* creator = NewObject<UCharacterCreateWidget>(world);
	creator->Initialize();
	creator->TakeWidget();
	const TArray<uint8> human = creator->packAppearance();
	TestEqual(TEXT("Human appearance retains the unused zero byte"), human[10], uint8(0));
	TArray<uint8> legacy = human;
	legacy[10] = 1;
	legacy[3] = 2;
	Network::FAppearance oldElf;
	TestTrue(TEXT("The first elf picker format still loads"), Network::FAppearance::unpack(legacy, oldElf));
	TestEqual(TEXT("An existing elf with legacy complexion index two maps to form C"), oldElf.head, uint8(2));
	TestEqual(TEXT("An existing elf starts at its original brightness"), oldElf.skinTone, uint8(0));
	creator->handleSpeciesNextClicked();
	if (!TestNotNull(TEXT("The creation screen contains the race selector"), creator->_speciesLabel.Get()))
	{
		return false;
	}

	TestTrue(TEXT("Elves have the same visible face selector as humans"), creator->_headLabel->GetParent()->GetVisibility() != ESlateVisibility::Collapsed);
	TestEqual(TEXT("New elves start with their original donor eyes"), creator->chosenAppearance().eyeColor, AppearanceChoices::donorEyeColor);
	creator->handleEyesNextClicked();
	TestEqual(TEXT("The original brown choice keeps its stored index"), creator->chosenAppearance().eyeColor, uint8(0));
	creator->handleEyesPrevClicked();
	TestEqual(TEXT("The original eyes can be reselected"), creator->chosenAppearance().eyeColor, AppearanceChoices::donorEyeColor);
	TestEqual(TEXT("An elf starts with the human hair colour"), creator->chosenAppearance().hairColor, uint8(0));
	creator->handleHairColorNextClicked();
	TestEqual(TEXT("Elf hair colour can be changed independently"), creator->chosenAppearance().hairColor, uint8(1));
	creator->handleHairColorPrevClicked();
	for (const bool male : { false, true })
	{
		if (male)
		{
			creator->handleMaleClicked();
		}
		else
		{
			creator->handleFemaleClicked();
		}

		const uint8 defaultHair = creator->chosenAppearance().hairStyle;
		TestEqual(TEXT("The first elf has the selected side-part hairstyle"), defaultHair, uint8(male ? 28 : 12));
		creator->handleHeadNextClicked();
		TestEqual(TEXT("The second elf retains its original creator hairstyle"), creator->chosenAppearance().hairStyle, uint8(0));
		creator->handleHeadPrevClicked();
		TestEqual(TEXT("Returning to the first face restores its default style"), creator->chosenAppearance().hairStyle, defaultHair);
		creator->handleHairNextClicked();
		const uint8 selectedHair = creator->chosenAppearance().hairStyle;
		TestTrue(TEXT("A player can change the elf hairstyle"), selectedHair != defaultHair);
		creator->handleHeadNextClicked();
		TestEqual(TEXT("An explicit hairstyle choice carries across elf faces"), creator->chosenAppearance().hairStyle, selectedHair);
		creator->handleHeadPrevClicked();
		creator->handleHairPrevClicked();

		for (uint8 head = 0; head < AppearanceChoices::heads; ++head)
		{
			const uint8 nativeBrows[][4] = { { 16, 1, 16, 1 }, { 6, 8, 9, 13 } };
			TestEqual(TEXT("Each face starts with its original brow style"), creator->chosenAppearance().eyebrows, nativeBrows[male ? 1 : 0][head]);
			const TCHAR* const suffixes[] = { TEXT(""), TEXT("B"), TEXT("C"), TEXT("D") };
			const FString name = FString(male ? TEXT("MH_ElfMale") : TEXT("MH_ElfFemale")) + suffixes[head];
			const TCHAR* const femaleDonors[] = { TEXT("Jelena"), TEXT("Vivian"), TEXT("Aera"), TEXT("Ada") };
			const TCHAR* const maleDonors[] = { TEXT("Victor"), TEXT("Orlando"), TEXT("Kelvin"), TEXT("Cameron") };
			const FString donorName = FString(TEXT("MH_")) + (male ? maleDonors[head] : femaleDonors[head]);
			const FString path = FString::Printf(TEXT("/Game/Characters/MetaHumans/Elves/%s/%s/BP_%s.BP_%s_C"), male ? TEXT("Male") : TEXT("Female"), male ? maleDonors[head] : femaleDonors[head], *name, *name);
			UClass* expectedClass = LoadClass<AActor>(nullptr, *path);
			if (!TestNotNull(*name, expectedClass))
			{
				return false;
			}

			AActor* player = world->SpawnActor<AActor>(expectedClass);
			USkeletalMeshComponent* face = FindObjectFast<USkeletalMeshComponent>(player, TEXT("Face"));
			USkeletalMeshComponent* body = FindObjectFast<USkeletalMeshComponent>(player, TEXT("Body"));
			if (!TestNotNull(TEXT("Elf face mesh"), face) || !TestNotNull(TEXT("Elf body mesh"), body))
			{
				return false;
			}

			const FString donorPath = FString::Printf(TEXT("/Game/Characters/MetaHumans/Humans/%s/%s/BP_%s.BP_%s_C"), male ? TEXT("Male") : TEXT("Female"), male ? maleDonors[head] : femaleDonors[head], *donorName, *donorName);
			UClass* donorClass = LoadClass<AActor>(nullptr, *donorPath);
			if (!TestNotNull(*donorName, donorClass))
			{
				return false;
			}

			AActor* donor = world->SpawnActor<AActor>(donorClass);
			USkeletalMeshComponent* donorFace = FindObjectFast<USkeletalMeshComponent>(donor, TEXT("Face"));
			USkeletalMeshComponent* donorBody = FindObjectFast<USkeletalMeshComponent>(donor, TEXT("Body"));
			if (!TestNotNull(TEXT("Human face"), donorFace) || !TestNotNull(TEXT("Human body"), donorBody))
			{
				return false;
			}

			Network::FAppearance eyeChoice = creator->chosenAppearance();
			for (const FName slot : { FName(TEXT("eyeLeft_shader_shader")), FName(TEXT("eyeRight_shader_shader")) })
			{
				const int32 index = face->GetMaterialIndex(slot);
				UMaterialInterface* original = donorFace->GetMaterial(donorFace->GetMaterialIndex(slot));
				if (UMaterialInstanceDynamic* nativeEye = Cast<UMaterialInstanceDynamic>(original))
				{
					original = nativeEye->Parent;
				}

				if (!TestTrue(TEXT("The eye slot exists"), index != INDEX_NONE) || !TestNotNull(TEXT("The human has an original eye material"), original))
				{
					return false;
				}

				eyeChoice.eyeColor = 3;
				Appearance::apply(*player, eyeChoice);
				TestTrue(TEXT("An explicit eye colour overrides the donor eye"), face->GetMaterial(index) != original);
				eyeChoice.eyeColor = AppearanceChoices::donorEyeColor;
				Appearance::apply(*player, eyeChoice);
				UMaterialInterface* restoredEye = face->GetMaterial(index);
				if (UMaterialInstanceDynamic* dynamicEye = Cast<UMaterialInstanceDynamic>(restoredEye))
				{
					restoredEye = dynamicEye->Parent;
				}

				TestTrue(TEXT("Reselecting original restores the corresponding human eye"), restoredEye == original);
			}

			float previousShade = 2.0f;
			for (uint8 tone = 0; tone < AppearanceChoices::skinTones; ++tone)
			{
				Network::FAppearance restored;
				TestTrue(TEXT("Saved player appearance decodes"), Network::FAppearance::unpack(creator->packAppearance(), restored));
				TestTrue(TEXT("The player is an elf"), restored.species == Network::EAppearanceSpecies::Elf);
				TestEqual(TEXT("Sex survives the round trip"), restored.male, male);
				TestEqual(TEXT("Changing the tone retains the selected face"), restored.head, head);
				TestEqual(TEXT("Changing the face and tone retains the selected hair colour"), restored.hairColor, uint8(0));
				TestEqual(TEXT("The selected elf skin survives the round trip"), restored.skinTone, tone);
				const TSharedRef<bool> matched = MakeShared<bool>(false);
				Appearance::requestBodyClass(restored, [matched, expectedClass](UClass* resolved)
				{
					*matched = (resolved == expectedClass);
				});
				TestTrue(TEXT("The stored appearance selects the loaded elf Blueprint"), *matched);
				Appearance::apply(*player, restored);
				const int32 faceSlot = face->GetMaterialIndex(TEXT("head_LOD1_shader_shader"));
				const int32 bodySlot = body->GetMaterialIndex(TEXT("body_shader_shader"));
				UMaterialInstanceDynamic* faceSkin = Cast<UMaterialInstanceDynamic>(face->GetMaterial(faceSlot));
				UMaterialInstanceDynamic* bodySkin = Cast<UMaterialInstanceDynamic>(body->GetMaterial(bodySlot));
				if (!TestNotNull(TEXT("Elf face has tinted skin"), faceSkin) || !TestNotNull(TEXT("Elf body has tinted skin"), bodySkin))
				{
					return false;
				}

				Network::FAppearance humanAppearance = restored;
				humanAppearance.species = Network::EAppearanceSpecies::Human;
				Appearance::apply(*donor, humanAppearance);
				UMaterialInterface* originalFace = donorFace->GetMaterial(donorFace->GetMaterialIndex(TEXT("head_LOD1_shader_shader")));
				UMaterialInterface* originalBody = donorBody->GetMaterial(donorBody->GetMaterialIndex(TEXT("body_shader_shader")));
				const FName multiplyParameter(TEXT("Basecolor Global Multiply Post-Bake"));
				const FName valueParameter(TEXT("Basecolor Global Value Post-Bake"));
				FLinearColor originalFaceColour;
				FLinearColor originalBodyColour;
				float originalShade = 0.0f;
				if (!TestNotNull(TEXT("Human face skin"), originalFace) || !TestNotNull(TEXT("Human body skin"), originalBody))
				{
					return false;
				}

				TestTrue(TEXT("Human face exposes its skin colour"), originalFace->GetVectorParameterValue(multiplyParameter, originalFaceColour));
				TestTrue(TEXT("Human body exposes its skin colour"), originalBody->GetVectorParameterValue(multiplyParameter, originalBodyColour));
				const FLinearColor elfFaceColour = faceSkin->K2_GetVectorParameterValue(multiplyParameter);
				const FLinearColor elfBodyColour = bodySkin->K2_GetVectorParameterValue(multiplyParameter);
				const float lift = AppearanceChoices::elfSkinBrightness;
				const FLinearColor multiplier(lift, lift, lift, 1.0f);
				TestTrue(TEXT("Elf face retains the original complexion"), elfFaceColour.Equals(originalFaceColour * multiplier));
				TestTrue(TEXT("Body and face use the same neutral lift"), elfBodyColour.Equals(originalBodyColour * multiplier));
				TestTrue(TEXT("Human face exposes its shade"), originalFace->GetScalarParameterValue(valueParameter, originalShade));
				const float shade = faceSkin->K2_GetScalarParameterValue(valueParameter);
				TestEqual(TEXT("Elf and human use the same natural shade"), shade, originalShade);
				TestTrue(TEXT("Each successive shade is darker"), shade < previousShade);
				TestEqual(TEXT("Body and face use the same shade"), bodySkin->K2_GetScalarParameterValue(valueParameter), shade);
				previousShade = shade;
				creator->handleSkinNextClicked();
			}

			creator->handleHeadNextClicked();
		}
	}

	creator->handleSpeciesPrevClicked();
	TestTrue(TEXT("Returning to human restores its original appearance bytes"), creator->packAppearance() == human);
	creator->handleSpeciesNextClicked();
	creator->handleHeadPrevClicked();
	creator->handleSpeciesPrevClicked();
	creator->handleSpeciesNextClicked();
	TestEqual(TEXT("Switching species preserves the selected face index"), creator->chosenAppearance().head, uint8(3));
	return true;
}

#endif
