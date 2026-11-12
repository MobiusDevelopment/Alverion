#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSingleNodeInstance.h"
#include "Appearance/AppearanceApplier.h"
#include "Appearance/AppearanceChoices.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/World.h"
#include "FrontEnd/CharacterCreateWidget.h"
#include "FrontEnd/FrontEndGameMode.h"
#include "GameFramework/Actor.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "GroomComponent.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Tickable.h"
#include "UI/LocalizedText.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrcPlayerCreationTest, "Alverion.Characters.OrcPlayerCreation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrcAppearanceCompatibilityTest, "Alverion.Characters.OrcAppearanceCompatibility", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOrcAppearanceCompatibilityTest::RunTest(const FString& parameters)
{
	Network::FAppearance original;
	original.species = Network::EAppearanceSpecies::Orc;
	original.head = 3;
	original.skinTone = 5;
	TArray<uint8> bytes = original.pack();
	TestEqual(TEXT("The appearance keeps the existing packet length"), bytes.Num(), 24);
	TestEqual(TEXT("The orc uses the spare species byte"), bytes[10], uint8(2));
	Network::FAppearance restored;
	bytes[11] = 0;
	TestTrue(TEXT("The old elf marker is ignored on orcs"), Network::FAppearance::unpack(bytes, restored));
	TestTrue(TEXT("An orc retains independent face and shade indices"), (restored.head == 3) && (restored.skinTone == 5));
	bytes[10] = 1;
	bytes[3] = 2;
	TestTrue(TEXT("The original elf format still decodes"), Network::FAppearance::unpack(bytes, restored));
	TestTrue(TEXT("A legacy elf complexion becomes its face index"), (restored.species == Network::EAppearanceSpecies::Elf) && (restored.head == 2) && (restored.skinTone == 0));
	bytes[10] = 255;
	TestTrue(TEXT("An unrecognized species retains the rest of the appearance"), Network::FAppearance::unpack(bytes, restored));
	TestTrue(TEXT("An unrecognized species renders as human"), (restored.species == Network::EAppearanceSpecies::Human) && (restored.head == 3) && (restored.skinTone == 2));
	return true;
}

bool FOrcPlayerCreationTest::RunTest(const FString& parameters)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(world);
	ON_SCOPE_EXIT
	{
		world->EndPlay(EEndPlayReason::EndPlayInEditor);
		world->DestroyWorld(false);
		GEngine->DestroyWorldContext(world);
	};

	AFrontEndGameMode* frontEnd = world->SpawnActor<AFrontEndGameMode>();
	AActor* marker = world->SpawnActor<AActor>();
	frontEnd->_avatarSpots.Add(EAvatarSpot::Create, marker);
	world->InitializeActorsForPlay(FURL());
	world->SetBegunPlay(true);
	UCharacterCreateWidget* creator = NewObject<UCharacterCreateWidget>(world);
	creator->Initialize();
	creator->TakeWidget();
	const TArray<uint8> human = creator->packAppearance();
	creator->handleSpeciesPrevClicked();
	if (!TestNotNull(TEXT("The creator displays a species selector"), creator->_speciesLabel.Get()) || !TestTrue(TEXT("Previous from Human selects Orc"), creator->_speciesLabel->GetText().EqualTo(LocalizedText::get(TEXT("charcreate.species_orc")))))
	{
		return false;
	}

	creator->handleSpeciesNextClicked();
	TestTrue(TEXT("Next from Orc restores the original human"), creator->packAppearance() == human);
	creator->handleSpeciesNextClicked();
	TestTrue(TEXT("Next from Human selects Elf"), creator->chosenAppearance().species == Network::EAppearanceSpecies::Elf);
	creator->handleSpeciesNextClicked();
	TestTrue(TEXT("Next from Elf selects Dwarf"), creator->chosenAppearance().species == Network::EAppearanceSpecies::Dwarf);
	creator->handleSpeciesNextClicked();
	creator->handleHairColorNextClicked();
	const uint8 hairColor = creator->chosenAppearance().hairColor;
	TestEqual(TEXT("An orc can choose a hair colour"), hairColor, uint8(1));
	const TCHAR* const suffixes[] = { TEXT(""), TEXT("B"), TEXT("C"), TEXT("D") };
	const TCHAR* const donors[][4] = { { TEXT("Jelena"), TEXT("Vivian"), TEXT("Aera"), TEXT("Ada") }, { TEXT("Victor"), TEXT("Orlando"), TEXT("Kelvin"), TEXT("Cameron") } };
	const FLinearColor donorGreens[][4] =
	{
		{ FLinearColor(0.585f, 0.755f, 0.46f), FLinearColor(0.585f, 0.765f, 0.43f), FLinearColor(0.605f, 0.775f, 0.485f), FLinearColor(0.515f, 0.78f, 0.39f) },
		{ FLinearColor(0.55f, 0.745f, 0.435f), FLinearColor(0.545f, 0.75f, 0.415f), FLinearColor(0.57f, 0.765f, 0.45f), FLinearColor(0.485f, 0.78f, 0.375f) }
	};
	const FName skinSlots[] = { TEXT("head_LOD1_shader_shader"), TEXT("head_LOD3_shader_shader"), TEXT("head_LOD57_shader_shader"), TEXT("body_shader_shader") };
	const float humanShades[] = { 1.00f, 0.85f, 0.70f, 0.55f, 0.42f, 0.30f };
	const FName hueParameter(TEXT("Basecolor Global Multiply Post-Bake"));
	const FName saturationParameter(TEXT("Basecolor Global Saturation Post-Bake"));
	const FName shadeParameter(TEXT("Basecolor Global Value Post-Bake"));
	static_assert(UE_ARRAY_COUNT(humanShades) == AppearanceChoices::skinTones);
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

		for (uint8 head = 0; head < UE_ARRAY_COUNT(suffixes); ++head)
		{
			const FString name = FString(male ? TEXT("MH_OrcMale") : TEXT("MH_OrcFemale")) + suffixes[head];
			const FString path = FString::Printf(TEXT("/Game/Characters/MetaHumans/Orcs/%s/%s/BP_%s.BP_%s_C"), male ? TEXT("Male") : TEXT("Female"), donors[male ? 1 : 0][head], *name, *name);
			UClass* expected = LoadClass<AActor>(nullptr, *path);
			if (!TestNotNull(*name, expected))
			{
				return false;
			}

			const FText faceLabel = FText::Format(LocalizedText::get(TEXT("charcreate.value_type")), FText::FromString(FString::Chr(TEXT('A') + head)));
			TestTrue(TEXT("The face label uses the human A through D choices"), creator->_headLabel->GetText().EqualTo(faceLabel));
			const FString donorName = FString(TEXT("MH_")) + donors[male ? 1 : 0][head];
			const FString donorPath = FString::Printf(TEXT("/Game/Characters/MetaHumans/Humans/%s/%s/BP_%s.BP_%s_C"), male ? TEXT("Male") : TEXT("Female"), donors[male ? 1 : 0][head], *donorName, *donorName);
			UClass* donorClass = LoadClass<AActor>(nullptr, *donorPath);
			if (!TestNotNull(*donorName, donorClass))
			{
				return false;
			}

			const Network::FAppearance orcLook = creator->chosenAppearance();
			Network::FAppearance humanLook = orcLook;
			humanLook.species = Network::EAppearanceSpecies::Human;
			frontEnd->standAvatar(EAvatarSpot::Create, donorClass, humanLook);
			USkeletalMeshComponent* humanPreview = FindObjectFast<USkeletalMeshComponent>(frontEnd->_avatar.Get(), TEXT("Body"));
			if (!TestNotNull(TEXT("The creator stands the human preview"), humanPreview))
			{
				return false;
			}

			const double humanHead = humanPreview->GetSocketLocation(TEXT("head")).Z - marker->GetActorLocation().Z;
			const double humanFoot = humanPreview->GetSocketLocation(TEXT("foot_l")).Z - marker->GetActorLocation().Z;
			frontEnd->standAvatar(EAvatarSpot::Create, expected, orcLook);
			AActor* player = frontEnd->_avatar.Get();
			USkeletalMeshComponent* face = FindObjectFast<USkeletalMeshComponent>(player, TEXT("Face"));
			USkeletalMeshComponent* body = FindObjectFast<USkeletalMeshComponent>(player, TEXT("Body"));
			if (!TestNotNull(TEXT("The orc has a face"), face) || !TestNotNull(TEXT("The orc has a body"), body) || !TestNotNull(TEXT("The face has a mesh"), face->GetSkeletalMeshAsset()) || !TestNotNull(TEXT("The body has a mesh"), body->GetSkeletalMeshAsset()))
			{
				return false;
			}

			TestEqual(TEXT("The face uses the sculpted orc mesh"), face->GetSkeletalMeshAsset()->GetName(), FString::Printf(TEXT("SKM_%s_FaceMesh"), *name));
			TestEqual(TEXT("The body uses the corresponding orc sculpt"), body->GetSkeletalMeshAsset()->GetName(), FString::Printf(TEXT("SKM_%s_BodyMesh"), *name));
			TestTrue(TEXT("The creator stands the orc at the same foot origin"), player->GetActorLocation().Equals(marker->GetActorLocation(), 0.01));
			const double orcHead = body->GetSocketLocation(TEXT("head")).Z - marker->GetActorLocation().Z;
			TestEqual(TEXT("The creator shows eight percent more height"), orcHead, humanHead * 1.08, 0.15);
			TestEqual(TEXT("The ankle scales from the same foot origin"), body->GetSocketLocation(TEXT("foot_l")).Z - marker->GetActorLocation().Z, humanFoot * 1.08, 0.15);
			frontEnd->standAvatar(EAvatarSpot::Create, expected, orcLook);
			TestTrue(TEXT("Refreshing the same orc reuses its preview"), frontEnd->_avatar.Get() == player);
			TestEqual(TEXT("Refreshing the preview preserves its height"), body->GetSocketLocation(TEXT("head")).Z - marker->GetActorLocation().Z, orcHead, 0.01);
			const int32 tuskSlot = face->GetMaterialIndex(TEXT("Tusks"));
			const FSkeletalMeshRenderData* renderData = face->GetSkeletalMeshRenderData();
			if (!TestTrue(TEXT("The tusks have their own material slot"), tuskSlot != INDEX_NONE) || !TestNotNull(TEXT("The orc has tusk enamel"), face->GetMaterial(tuskSlot)) || !TestNotNull(TEXT("The face has render geometry"), renderData))
			{
				return false;
			}

			TestEqual(TEXT("The tusk draw sections use the authored enamel"), face->GetMaterial(tuskSlot)->GetName(), FString(TEXT("M_OrcTusk")));
			for (int32 lodIndex = 0; lodIndex < renderData->LODRenderData.Num(); ++lodIndex)
			{
				const FSkeletalMeshLODRenderData& lod = renderData->LODRenderData[lodIndex];
				const FSkeletalMeshLODInfo* info = face->GetSkeletalMeshAsset()->GetLODInfo(lodIndex);
				uint32 tuskTriangles = 0;
				for (int32 sectionIndex = 0; sectionIndex < lod.RenderSections.Num(); ++sectionIndex)
				{
					const FSkelMeshRenderSection& section = lod.RenderSections[sectionIndex];
					int32 materialIndex = section.MaterialIndex;
					if ((info != nullptr) && info->LODMaterialMap.IsValidIndex(sectionIndex))
					{
						const int32 mapped = info->LODMaterialMap[sectionIndex];
						if (face->GetSkeletalMeshAsset()->IsValidMaterialIndex(mapped))
						{
							materialIndex = mapped;
						}
					}

					if (!section.bDisabled && (materialIndex == tuskSlot))
					{
						tuskTriangles += section.NumTriangles;
					}
				}

				TestTrue(*FString::Printf(TEXT("%s LOD %d renders tusk enamel"), *name, lodIndex), tuskTriangles > 0);
			}

			AActor* donor = world->SpawnActor<AActor>(donorClass);
			USkeletalMeshComponent* donorFace = FindObjectFast<USkeletalMeshComponent>(donor, TEXT("Face"));
			USkeletalMeshComponent* donorBody = FindObjectFast<USkeletalMeshComponent>(donor, TEXT("Body"));
			if (!TestNotNull(TEXT("The human donor has a face"), donorFace) || !TestNotNull(TEXT("The human donor has a body"), donorBody))
			{
				return false;
			}

			// The hands sit outside the waist in the reference pose. Measure the torso in the same horizontal slice on both meshes.
			FBox shoulderBounds[2] = { FBox(ForceInit), FBox(ForceInit) };
			FBox waistBounds[2] = { FBox(ForceInit), FBox(ForceInit) };
			USkeletalMeshComponent* const waistMeshes[] = { donorBody, body };
			for (int32 meshIndex = 0; meshIndex < UE_ARRAY_COUNT(waistMeshes); ++meshIndex)
			{
				const FSkeletalMeshRenderData* bodyData = waistMeshes[meshIndex]->GetSkeletalMeshRenderData();
				if (!TestNotNull(TEXT("The body has render data for its waist measurement"), bodyData) || !TestTrue(TEXT("The body has a top level of detail"), !bodyData->LODRenderData.IsEmpty()))
				{
					return false;
				}

				const FPositionVertexBuffer& positions = bodyData->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;
				for (uint32 vertex = 0; vertex < positions.GetNumVertices(); ++vertex)
				{
					const FVector3f point = positions.VertexPosition(vertex);
					if (FMath::Abs(point.Z - 141.0f) < 1.0f)
					{
						shoulderBounds[meshIndex] += FVector(point);
					}

					if ((FMath::Abs(point.Z - 109.0f) < 1.0f) && (FMath::Abs(point.X) < 24.0f))
					{
						waistBounds[meshIndex] += FVector(point);
					}
				}
			}

			if (!TestTrue(TEXT("The two torso slices contain surface vertices"), waistBounds[0].IsValid && waistBounds[1].IsValid && shoulderBounds[0].IsValid && shoulderBounds[1].IsValid))
			{
				return false;
			}

			const FVector humanWaist = waistBounds[0].GetSize();
			const FVector orcWaist = waistBounds[1].GetSize();
			TestTrue(TEXT("The competition waist remains within ten percent of donor width"), FMath::IsNearlyEqual(orcWaist.X, humanWaist.X, humanWaist.X * 0.10));
			TestTrue(TEXT("The conditioned abdomen remains within fifteen percent of donor depth"), FMath::IsNearlyEqual(orcWaist.Y, humanWaist.Y, humanWaist.Y * 0.15));
			const double orcTaper = shoulderBounds[1].GetSize().X / orcWaist.X;

			// These values record the approved male and female body silhouettes.
			TestEqual(TEXT("The orc retains the approved shoulder-to-waist taper"), orcTaper, male ? 2.00419 : 1.72654, 0.002);
			for (uint8 tone = 0; tone < AppearanceChoices::skinTones; ++tone)
			{
				const TArray<uint8> bytes = creator->packAppearance();
				Network::FAppearance saved;
				if (!TestTrue(TEXT("The saved appearance decodes"), Network::FAppearance::unpack(bytes, saved)))
				{
					return false;
				}

				TestEqual(TEXT("The wire stores the orc species"), bytes[10], uint8(2));
				TestTrue(TEXT("The orc survives a round trip"), saved.species == Network::EAppearanceSpecies::Orc);
				TestEqual(TEXT("The selected sex persists"), saved.male, male);
				TestEqual(TEXT("The selected face persists"), saved.head, head);
				TestEqual(TEXT("The selected shade persists"), saved.skinTone, tone);
				TestEqual(TEXT("The selected hair colour persists"), saved.hairColor, hairColor);
				TestEqual(TEXT("Orcs start with donor eyes"), saved.eyeColor, AppearanceChoices::donorEyeColor);
				const TSharedRef<bool> matched = MakeShared<bool>(false);
				Appearance::requestBodyClass(saved, [matched, expected](UClass* loaded)
				{
					*matched = (loaded == expected);
				});
				TestTrue(TEXT("The saved appearance resolves to its orc Blueprint"), *matched);
				Appearance::apply(*player, saved);
				for (const FName slot : skinSlots)
				{
					const bool bodySlot = slot == TEXT("body_shader_shader");
					USkeletalMeshComponent* orcMesh = bodySlot ? body : face;
					USkeletalMeshComponent* donorMesh = bodySlot ? donorBody : donorFace;
					UMaterialInterface* skin = orcMesh->GetMaterial(orcMesh->GetMaterialIndex(slot));
					const int32 donorSlot = donorMesh->GetMaterialIndex(slot);
					UMaterialInterface* original = donorMesh->GetSkeletalMeshAsset()->GetMaterials().IsValidIndex(donorSlot) ? donorMesh->GetSkeletalMeshAsset()->GetMaterials()[donorSlot].MaterialInterface.Get() : nullptr;
					if (!TestNotNull(*slot.ToString(), skin) || !TestNotNull(TEXT("The original donor skin is present"), original))
					{
						return false;
					}

					const UMaterialInstance* appliedSkin = Cast<UMaterialInstance>(skin);
					const UMaterialInstance* greenSkin = appliedSkin ? Cast<UMaterialInstance>(appliedSkin->Parent.Get()) : nullptr;
					if (!TestNotNull(TEXT("The selected shade has an orc skin parent"), greenSkin))
					{
						return false;
					}

					const FString skinFolder = FString::Printf(TEXT("/Game/Characters/MetaHumans/Orcs/%s/%s/Skin/"), male ? TEXT("Male") : TEXT("Female"), donors[male ? 1 : 0][head]);
					TestTrue(TEXT("The green skin belongs to the selected orc"), greenSkin->GetPathName().StartsWith(skinFolder));
					TestTrue(TEXT("The green skin directly inherits the corresponding human donor slot"), greenSkin->Parent.Get() == original);
					FLinearColor hue = FLinearColor::Black;
					FLinearColor authoredHue = FLinearColor::Black;
					float saturation = 0.0f;
					float shade = 0.0f;
					TestTrue(TEXT("The authored skin exposes its green multiplier"), greenSkin->GetVectorParameterValue(hueParameter, authoredHue));
					TestTrue(TEXT("The selected donor has the matching olive skin multiplier"), authoredHue.Equals(donorGreens[male ? 1 : 0][head]));
					TestTrue(TEXT("The selected human shade retains the authored green multiplier"), skin->GetVectorParameterValue(hueParameter, hue) && hue.Equals(authoredHue));
					TestTrue(TEXT("The green skin exposes its saturation"), skin->GetScalarParameterValue(saturationParameter, saturation));
					TestEqual(TEXT("The orc uses the muted green saturation"), saturation, -0.34f);
					TestTrue(TEXT("The skin exposes its shade"), skin->GetScalarParameterValue(shadeParameter, shade));
					TestEqual(TEXT("Each skin slot applies the selected human shade"), shade, humanShades[tone]);
					if (tone == 0)
					{
						TArray<FMaterialParameterInfo> textures;
						TArray<FGuid> ids;
						original->GetAllTextureParameterInfo(textures, ids);
						TestTrue(TEXT("The donor skin has texture parameters"), !textures.IsEmpty());
						for (const FMaterialParameterInfo& texture : textures)
						{
							UTexture* donorTexture = nullptr;
							UTexture* orcTexture = nullptr;
							TestEqual(TEXT("The orc resolves the same skin texture parameter"), skin->GetTextureParameterValue(texture, orcTexture), original->GetTextureParameterValue(texture, donorTexture));
							TestTrue(*texture.Name.ToString(), orcTexture == donorTexture);
						}
					}
				}

				creator->handleSkinNextClicked();
			}

			FlushAsyncLoading();
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.016f);
			for (const FName slot : { FName(TEXT("Hair")), FName(TEXT("Eyebrows")) })
			{
				UGroomComponent* groom = FindObjectFast<UGroomComponent>(player, slot);
				if (!TestNotNull(*slot.ToString(), groom) || !TestNotNull(TEXT("The selected groom loads"), groom->GroomAsset.Get()) || !TestNotNull(TEXT("The selected binding loads"), groom->BindingAsset.Get()))
				{
					return false;
				}

				TestTrue(TEXT("The groom binding targets this orc face"), groom->BindingAsset->GetTargetSkeletalMesh() == face->GetSkeletalMeshAsset());
				TestTrue(TEXT("The chosen groom matches its binding"), groom->BindingAsset->GetGroom() == groom->GroomAsset.Get());
			}

			if (male && (head == 0))
			{
				creator->handleBeardNextClicked();
				Network::FAppearance pending = creator->chosenAppearance();
				pending.hairStyle = 29;
				Appearance::apply(*player, pending);
				creator->handleHairPrevClicked();
				Network::FAppearance bald;
				TestTrue(TEXT("The bald appearance decodes"), Network::FAppearance::unpack(creator->packAppearance(), bald));
				TestEqual(TEXT("Previous from the first orc hairstyle saves Bald"), bald.hairStyle, AppearanceChoices::orcBaldHairStyle);
				TestTrue(TEXT("The orc hairstyle label displays Bald"), creator->_hairStyleLabel->GetText().EqualTo(LocalizedText::get(TEXT("charcreate.hair_bald"))));
				TestEqual(TEXT("Bald preserves the chosen eyebrows"), bald.eyebrows, pending.eyebrows);
				TestEqual(TEXT("Bald preserves the chosen beard"), bald.beard, uint8(1));
				Appearance::apply(*player, bald);
				FlushAsyncLoading();
				FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.016f);
				UGroomComponent* hair = FindObjectFast<UGroomComponent>(player, TEXT("Hair"));
				UGroomComponent* brows = FindObjectFast<UGroomComponent>(player, TEXT("Eyebrows"));
				UGroomComponent* beard = FindObjectFast<UGroomComponent>(player, TEXT("Beard"));
				if (!TestNotNull(TEXT("The hair slot exists"), hair) || !TestNotNull(TEXT("The eyebrow slot exists"), brows) || !TestNotNull(TEXT("The beard slot exists"), beard))
				{
					return false;
				}

				TestNull(TEXT("A pending hairstyle cannot regrow on a bald orc"), hair->GroomAsset.Get());
				TestNotNull(TEXT("A bald orc still wears eyebrows"), brows->GroomAsset.Get());
				TestNotNull(TEXT("A bald orc still wears its beard"), beard->GroomAsset.Get());
				creator->handleHairNextClicked();
				creator->handleBeardPrevClicked();
				const Network::FAppearance regrown = creator->chosenAppearance();
				TestEqual(TEXT("The next hairstyle after Bald is the first one"), regrown.hairStyle, uint8(0));
				Appearance::apply(*player, regrown);
				FlushAsyncLoading();
				FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.016f);
				TestNotNull(TEXT("A hairstyle can be worn again after Bald"), hair->GroomAsset.Get());
			}

			Appearance::animate(*player, Appearance::EMotion::Run, 0.0f);
			UAnimSingleNodeInstance* animation = body->GetSingleNodeInstance();
			if (!TestNotNull(TEXT("The body plays its locomotion clip"), animation))
			{
				return false;
			}

			double lowestFoot = MAX_dbl;
			double highestFoot = -MAX_dbl;
			for (int32 frame = 0; frame <= 8; ++frame)
			{
				body->SetPosition(animation->GetLength() * frame / 8.0f, false);
				body->TickAnimation(0.0f, false);
				body->RefreshBoneTransforms();
				face->TickAnimation(0.0f, false);
				face->RefreshBoneTransforms();
				TestTrue(TEXT("The face follows the body at the neck"), body->GetSocketLocation(TEXT("neck_01")).Equals(face->GetSocketLocation(TEXT("neck_01")), 0.1));
				const double foot = body->GetSocketLocation(TEXT("foot_l")).Z;
				lowestFoot = FMath::Min(lowestFoot, foot);
				highestFoot = FMath::Max(highestFoot, foot);
			}

			TestTrue(TEXT("The foot lifts through the stride"), highestFoot - lowestFoot > 4.0);
			donor->Destroy();
			frontEnd->clearAvatar();
			creator->handleHeadNextClicked();
		}
	}

	creator->handleSpeciesNextClicked();
	TestTrue(TEXT("Orc customization preserves the human appearance"), human == creator->packAppearance());
	return true;
}

#endif
