#if WITH_DEV_AUTOMATION_TESTS

#include "Appearance.h"
#include "Appearance/AppearanceApplier.h"
#include "Appearance/AppearanceChoices.h"
#include "Components/TextBlock.h"
#include "FrontEnd/CharacterCreateWidget.h"
#include "UI/LocalizedText.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GroomAsset.h"
#include "GroomComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tickable.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FGroomSelectionTest, "Alverion.Characters.GroomSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FGroomSelectionTest::GetTests(TArray<FString>& names, TArray<FString>& commands) const
{
	for (const TCHAR* name : { TEXT("HumanMale"), TEXT("HumanFemale"), TEXT("ElfMale"), TEXT("ElfFemale"), TEXT("OrcMale"), TEXT("OrcFemale"), TEXT("DwarfMale"), TEXT("DwarfFemale") })
	{
		names.Add(name);
		commands.Add(name);
	}
}

bool FGroomSelectionTest::RunTest(const FString& parameters)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(world);
	ON_SCOPE_EXIT
	{
		world->DestroyWorld(false);
		GEngine->DestroyWorldContext(world);
	};

	const bool male = parameters.EndsWith(TEXT("Male"), ESearchCase::CaseSensitive);
	Network::FAppearance appearance;
	appearance.male = male;
	FString name = male ? TEXT("MH_Victor") : TEXT("MH_Jelena");
	const TCHAR* folder = TEXT("Humans");
	if (parameters.StartsWith(TEXT("Elf")))
	{
		appearance.species = Network::EAppearanceSpecies::Elf;
		folder = TEXT("Elves");
		name = male ? TEXT("MH_ElfMale") : TEXT("MH_ElfFemale");
	}
	else if (parameters.StartsWith(TEXT("Orc")))
	{
		appearance.species = Network::EAppearanceSpecies::Orc;
		folder = TEXT("Orcs");
		name = male ? TEXT("MH_OrcMale") : TEXT("MH_OrcFemale");
	}
	else if (parameters.StartsWith(TEXT("Dwarf")))
	{
		appearance.species = Network::EAppearanceSpecies::Dwarf;
	}

	const FString bodyPath = FString(TEXT("/Game/Characters/MetaHumans/")) + folder + (male ? TEXT("/Male/Victor/BP_") : TEXT("/Female/Jelena/BP_")) + name + TEXT(".BP_") + name + TEXT("_C");
	UClass* bodyClass = LoadClass<AActor>(nullptr, *bodyPath);
	if (!TestNotNull(TEXT("The player body is installed"), bodyClass))
	{
		return false;
	}

	AActor* player = world->SpawnActor<AActor>(bodyClass);
	UGroomComponent* hair = FindObjectFast<UGroomComponent>(player, TEXT("Hair"));
	if (!TestNotNull(TEXT("The player has a hair slot"), hair))
	{
		return false;
	}

	hair->SetGroomAsset(nullptr);
	hair->EmptyOverrideMaterials();
	appearance.hairStyle = 19;
	appearance.beard = 10;
	appearance.hairColor = 1;
	Appearance::apply(*player, appearance);
	AddInfo(hair->GroomAsset == nullptr ? TEXT("The brush cut is loading asynchronously.") : TEXT("The brush cut is cached."));
	appearance.beard = 0;
	appearance.hairColor = 4;
	Appearance::apply(*player, appearance);
	FlushAsyncLoading();
	FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.016f);
	UGroomComponent* beard = FindObjectFast<UGroomComponent>(player, TEXT("Beard"));
	if (!TestNotNull(TEXT("The beard component exists"), beard) || !TestNotNull(TEXT("The hair finished loading"), hair->GroomAsset.Get()))
	{
		return false;
	}

	TestNull(TEXT("Clearing the beard supersedes its pending load"), beard->GroomAsset.Get());
	TestEqual(TEXT("The brush cut is worn"), hair->GroomAsset->GetName(), FString(TEXT("Hair_S_BrushCut")));
	UMaterialInstanceDynamic* material = Cast<UMaterialInstanceDynamic>(hair->GetMaterial(0));
	if (TestNotNull(TEXT("The loaded hair has a colour override"), material))
	{
		TestEqual(TEXT("The loaded hair uses the latest blond colour"), material->K2_GetScalarParameterValue(TEXT("hairMelanin")), 0.12f);
	}

	appearance.hairStyle = 23;
	Appearance::apply(*player, appearance);
	appearance.hairStyle = 19;
	appearance.hairColor = 1;
	Appearance::apply(*player, appearance);
	FlushAsyncLoading();
	FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.016f);
	TestEqual(TEXT("A cached choice supersedes an earlier pending style"), hair->GroomAsset->GetName(), FString(TEXT("Hair_S_BrushCut")));
	material = Cast<UMaterialInstanceDynamic>(hair->GetMaterial(0));
	if (TestNotNull(TEXT("The cached hair has a colour override"), material))
	{
		TestEqual(TEXT("The cached hair can change to black"), material->K2_GetScalarParameterValue(TEXT("hairMelanin")), 1.0f);
	}

	UCharacterCreateWidget* creator = NewObject<UCharacterCreateWidget>(world);
	creator->Initialize();
	creator->TakeWidget();
	creator->_speciesIndex = static_cast<uint8>(appearance.species);
	creator->_sex = male ? 1 : 0;
	creator->_head = 0;
	creator->_hairStyle = 0;
	creator->_speciesHairStyles[creator->_speciesIndex][creator->_sex] = uint8(0);
	creator->cycleHair(-1);
	Network::FAppearance bald;
	TestTrue(TEXT("The bald appearance decodes"), Network::FAppearance::unpack(creator->packAppearance(), bald));
	TestEqual(TEXT("Every race cycles backward from the first hairstyle to Bald"), bald.hairStyle, uint8(34));
	TestTrue(TEXT("The hairstyle label displays Bald"), creator->_hairStyleLabel->GetText().EqualTo(LocalizedText::get(TEXT("charcreate.hair_bald"))));
	creator->cycleHair(1);
	TestEqual(TEXT("Every race cycles forward from Bald to the first hairstyle"), creator->chosenAppearance().hairStyle, uint8(0));
	appearance.hairStyle = 29;
	appearance.beard = male ? 1 : 0;
	Appearance::apply(*player, appearance);
	appearance.hairStyle = 34;
	Appearance::apply(*player, appearance);
	FlushAsyncLoading();
	FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.016f);
	TestFalse(TEXT("Bald hides the hair slot during resource compilation"), hair->IsVisible());
	TestNull(TEXT("Bald cancels any pending hair asset"), hair->GroomAsset.Get());
	TestNull(TEXT("Bald clears the hair binding"), hair->BindingAsset.Get());
	UGroomComponent* brows = FindObjectFast<UGroomComponent>(player, TEXT("Eyebrows"));
	if (TestNotNull(TEXT("The eyebrow slot exists"), brows))
	{
		TestNotNull(TEXT("Bald preserves eyebrows"), brows->GroomAsset.Get());
	}

	if (male)
	{
		TestNotNull(TEXT("Bald preserves the selected beard"), beard->GroomAsset.Get());
	}

	appearance.hairStyle = 19;
	Appearance::apply(*player, appearance);
	FlushAsyncLoading();
	FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.016f);
	TestNotNull(TEXT("A hairstyle can be worn again after Bald"), hair->GroomAsset.Get());
	TestTrue(TEXT("Choosing hair restores the slot visibility"), hair->IsVisible());

	return true;
}

#endif
