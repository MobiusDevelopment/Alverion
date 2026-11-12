#include "Appearance/AppearanceApplier.h"

#include "Appearance.h"
#include "Appearance/AppearanceChoices.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "Appearance/FootIkAnimInstance.h"
#include "Appearance/HeadwearHairComponent.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "GroomComponent.h"
#include "Items/ItemCatalog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/MemStack.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

namespace
{
	// How many times a run clip is posed to measure it, which is every 1/256 of a second for a loop of a bit under a second.
	constexpr int32 appearanceRunMeasureSamples = 240;

	// A foot counts as planted when it is this close to its lowest height, in centimetres.
	constexpr float appearancePlantedHeightCm = 2.0f;

	// Slower than this is a clip that does not travel, whatever it measured.
	constexpr float appearanceLeastClipSpeedCm = 20.0f;

	// A body that has just started, stopped or jumped to a new place measures a speed that says nothing about its stride.
	constexpr float appearanceLeastPlayRate = 0.5f;
	constexpr float appearanceGreatestPlayRate = 1.5f;

	// Another clip has to cut the foot slip to this share of the playing clip's, or the playing one stays.
	constexpr double appearanceClipSwapSlipShare = 0.85;

	struct FGroom
	{
		/** The kind folder the plugin files the groom under. */
		const TCHAR* category;

		/** The asset name, which the binding baked for each face is named after too. */
		const TCHAR* name;
	};

	/**
	 * Indexed by the hairStyle byte. Thirty four of the plugin's thirty eight: the receding and balding four were pulled on the operator's call,
	 * since one roster serves both sexes. Stored characters carry the byte, so the order only ever grows at the end.
	 */
	const FGroom hairStyles[] =
	{
		{ TEXT("Hair"), TEXT("Hair_S_Pixie") },
		{ TEXT("Hair"), TEXT("Hair_M_BobStraight") },
		{ TEXT("Hair"), TEXT("Hair_L_Straight") },
		{ TEXT("Hair"), TEXT("Hair_M_BobBangs") },
		{ TEXT("Hair"), TEXT("Hair_L_StraightBangs") },
		{ TEXT("Hair"), TEXT("Hair_S_BobLayered") },
		{ TEXT("Hair"), TEXT("Hair_L_MessyClumps") },
		{ TEXT("Hair"), TEXT("Hair_M_BobCurly") },
		{ TEXT("Hair"), TEXT("Hair_M_BobMessy") },
		{ TEXT("Hair"), TEXT("Hair_M_BobSlick") },
		{ TEXT("Hair"), TEXT("Hair_M_TwistedBraids") },
		{ TEXT("Hair"), TEXT("Hair_M_FauxMohawk") },
		{ TEXT("Hair"), TEXT("Hair_S_LowPonytail") },
		{ TEXT("Hair"), TEXT("Hair_S_Updo") },
		{ TEXT("Hair"), TEXT("Hair_S_UpdoBraids") },
		{ TEXT("Hair"), TEXT("Hair_S_UpdoBuns") },
		{ TEXT("Hair"), TEXT("Hair_S_Cornrows") },
		{ TEXT("Hair"), TEXT("Hair_S_Coil") },
		{ TEXT("Hair"), TEXT("Hair_S_CoilBuzzCut") },
		{ TEXT("Hair"), TEXT("Hair_S_BrushCut") },
		{ TEXT("Hair"), TEXT("Hair_L_AfroCurly") },
		{ TEXT("Hair"), TEXT("Hair_M_Layered") },
		{ TEXT("Hair"), TEXT("Hair_M_Mohawk") },
		{ TEXT("Hair"), TEXT("Hair_M_SideSweptFringe") },
		{ TEXT("Hair"), TEXT("Hair_S_SideSweptFringe") },
		{ TEXT("Hair"), TEXT("Hair_S_SweptUp") },
		{ TEXT("Hair"), TEXT("Hair_S_SlickBack") },
		{ TEXT("Hair"), TEXT("Hair_S_PulledBack") },
		{ TEXT("Hair"), TEXT("Hair_S_Casual") },
		{ TEXT("Hair"), TEXT("Hair_S_Messy") },
		{ TEXT("Hair"), TEXT("Hair_S_CurlyFade") },
		{ TEXT("Hair"), TEXT("Hair_S_AfroFade") },
		{ TEXT("Hair"), TEXT("Hair_S_360Waves") },
		{ TEXT("Hair"), TEXT("Hair_S_Clean") }
	};

	/** Indexed by the eyebrows byte: every brow groom with a binding baked for all ten faces. */
	const FGroom eyebrowStyles[] =
	{
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Natural") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_SlightArch") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Fine") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Thin") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_S_FlatThin") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Full") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Dense") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Thick") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_FlatThick") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Wide") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Swept") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Messy") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Unruly") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Slit") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_M_Shaded") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_S_Shaded") },
		{ TEXT("Eyebrows"), TEXT("Eyebrows_L_Shaded") }
	};

	/** Indexed by the beard byte less one, zero being a clean chin. */
	const FGroom beards[] =
	{
		{ TEXT("Beards"), TEXT("Beard_S_Stubble") },
		{ TEXT("Beards"), TEXT("Beard_M_Stubble") },
		{ TEXT("Beards"), TEXT("Beard_S_Uneven") },
		{ TEXT("Beards"), TEXT("Beard_S_PencilThin") },
		{ TEXT("Beards"), TEXT("Beard_S_Full") },
		{ TEXT("Beards"), TEXT("Beard_S_Curly") },
		{ TEXT("Beards"), TEXT("Beard_M_Curly") },
		{ TEXT("Beards"), TEXT("Beard_M_MuttonChops") },
		{ TEXT("Beards"), TEXT("Beard_L_Full") },
		{ TEXT("Beards"), TEXT("Beard_L_Messy") },
		{ TEXT("Beards"), TEXT("Goatee_S_SoulpatchStrip") },
		{ TEXT("Beards"), TEXT("Goatee_M_Soulpatch") },
		{ TEXT("Beards"), TEXT("Goatee_S_ChinStrap") },
		{ TEXT("Beards"), TEXT("Goatee_M_Pointed") },
		{ TEXT("Beards"), TEXT("Goatee_L_Wavy") },
		{ TEXT("Beards"), TEXT("Goatee_L_Scraggly") }
	};

	/** Indexed by the mustache byte less one, zero being a bare lip. */
	const FGroom mustaches[] =
	{
		{ TEXT("Mustaches"), TEXT("Mustache_S_Stubble") },
		{ TEXT("Mustaches"), TEXT("Mustache_M_Stubble") },
		{ TEXT("Mustaches"), TEXT("Mustache_L_Stubble") },
		{ TEXT("Mustaches"), TEXT("Mustache_S_PencilThin") },
		{ TEXT("Mustaches"), TEXT("Mustache_S_Uneven") },
		{ TEXT("Mustaches"), TEXT("Mustache_S_Full") },
		{ TEXT("Mustaches"), TEXT("Mustache_S_Curly") },
		{ TEXT("Mustaches"), TEXT("Mustache_M_Curly") },
		{ TEXT("Mustaches"), TEXT("Mustache_S_ChinStrap") },
		{ TEXT("Mustaches"), TEXT("Mustache_S_Horseshoe") },
		{ TEXT("Mustaches"), TEXT("Mustache_M_Gunslinger") },
		{ TEXT("Mustaches"), TEXT("Mustache_L_Handlebar") },
		{ TEXT("Mustaches"), TEXT("Mustache_L_Full") },
		{ TEXT("Mustaches"), TEXT("Mustache_L_Wavy") },
		{ TEXT("Mustaches"), TEXT("Mustache_L_Messy") },
		{ TEXT("Mustaches"), TEXT("Mustache_L_Scraggly") }
	};

	/** Melanin is how dark, from white at zero to black at one; redness is how red what remains is. Measured on screen, not reasoned. */
	struct FHairColour
	{
		float melanin;
		float redness;
	};

	/** Indexed by the hairColor byte: brown, black, auburn, red, blonde. */
	const FHairColour hairColours[] =
	{
		{ 0.75f, 0.25f },
		{ 1.00f, 0.00f },
		{ 0.45f, 1.00f },
		{ 0.25f, 1.00f },
		{ 0.12f, 0.35f }
	};

	/** The iris hue is a ramp lookup rather than a hue wheel, so these were measured on screen; saturation reaches grey and value reaches black. */
	struct FEyeColour
	{
		float hue;
		float value;
		float saturation;
	};

	/** Indexed by the eyeColor byte: brown, amber, green, blue, grey, black. */
	const FEyeColour eyeColours[] =
	{
		{ 0.90f, 0.56f, 1.70f },
		{ 0.62f, 0.65f, 1.90f },
		{ 0.35f, 0.60f, 1.80f },
		{ 0.08f, 0.62f, 1.80f },
		{ 0.10f, 0.60f, 0.25f },
		{ 0.90f, 0.10f, 0.60f }
	};

	/** Indexed by the skinTone byte: the post-bake value multiplier, one being the skin as built. Above one overflows into red. */
	const float skinTones[] = { 1.00f, 0.85f, 0.70f, 0.55f, 0.42f, 0.30f };

	/** Filed by sex, indexed by the head byte. Whole characters, picked by the operator from the plugin's presets. */
	const TCHAR* const femaleHeads[] = { TEXT("Jelena"), TEXT("Vivian"), TEXT("Aera"), TEXT("Ada") };
	const TCHAR* const maleHeads[] = { TEXT("Victor"), TEXT("Orlando"), TEXT("Kelvin"), TEXT("Cameron") };

	// The screen cycles up to these counts, so a table shorter than its count is a choice that does nothing.
	static_assert(UE_ARRAY_COUNT(hairStyles) == AppearanceChoices::hairStyles, "The hairstyle table and the creation screen's count have to agree.");
	static_assert(UE_ARRAY_COUNT(eyebrowStyles) == AppearanceChoices::eyebrows, "The eyebrow table and the creation screen's count have to agree.");
	static_assert((UE_ARRAY_COUNT(beards) + 1) == AppearanceChoices::beards, "The beard table plus the clean chin have to match the creation screen's count.");
	static_assert((UE_ARRAY_COUNT(mustaches) + 1) == AppearanceChoices::mustaches, "The moustache table plus the bare lip have to match the creation screen's count.");
	static_assert(UE_ARRAY_COUNT(hairColours) == AppearanceChoices::hairColors, "The hair colour table and the creation screen's count have to agree.");
	static_assert(UE_ARRAY_COUNT(eyeColours) == AppearanceChoices::eyeColors, "The eye colour table and the creation screen's count have to agree.");
	static_assert(UE_ARRAY_COUNT(skinTones) == AppearanceChoices::skinTones, "The skin tone table and the creation screen's count have to agree.");
	static_assert(UE_ARRAY_COUNT(femaleHeads) == AppearanceChoices::heads, "The female head table and the creation screen's count have to agree.");
	static_assert(UE_ARRAY_COUNT(maleHeads) == AppearanceChoices::heads, "The male head table and the creation screen's count have to agree.");

	const TCHAR* const charactersFolder = TEXT("/Game/Characters/MetaHumans");
	const TCHAR* const bindingFolder = TEXT("/Game/Characters/MetaHumans/Shared/Grooms");
	const TCHAR* const groomFolder = TEXT("/MetaHumanCharacter/Optional/Grooms/GroomAssets");
	const TCHAR* const femaleBaseName = TEXT("MH_HumanFemale");
	const TCHAR* const maleBaseName = TEXT("MH_HumanMale");

	// A dwarf is a human donor's own class, face, skin and grooms on a dwarf body and face mesh made from that donor, stood broader and shorter by the roster's scale.
	const TCHAR* const appearanceDwarfFolder = TEXT("/Game/Characters/MetaHumans/Dwarfs");
	const FVector appearanceDwarfScale(1.12, 1.08, 0.90);

	// The whole standing height against the donor's, since the dwarf mesh is itself shorter than the human one before the scale.
	constexpr float appearanceDwarfHeightRatio = 0.75f;

	/**
	 * How far a dwarf rises above each clip, in its own centimetres, measured against its donor's soles on the eight of them.
	 * Its reference pose stands with the legs spread, which sets its pelvis lower than the straighter legs of the clips reach.
	 */
	constexpr float appearanceDwarfStandLift = 1.0f;
	constexpr float appearanceDwarfWalkLift = 5.8f;
	constexpr float appearanceDwarfRunLift = 7.3f;

	// The sculpted orc sole extends below the walk clip's original contact.
	constexpr float appearanceOrcWalkLift = 1.0f;

	// On the body of a donor stood as a dwarf, so telling one costs a name rather than a path.
	const FName appearanceDwarfTag(TEXT("AlverionDwarf"));
	const FName appearanceOrcTag(TEXT("AlverionOrc"));
	const FName starterGarmentTag(TEXT("AlverionStarterGarment"));
	const FName headwearHairTag(TEXT("AlverionHeadwearHair"));

	/** The baked eye material ignores colour, so the eye slots get an instance of this parametric one instead. */
	const TCHAR* const parametricEyePath = TEXT("/MetaHumanCharacter/Lookdev_UHM/Eye/Materials/MI_eye_eyeball_unified_MH_preset_left.MI_eye_eyeball_unified_MH_preset_left");

	// Named exactly as the materials name them, spaces and all.
	const FName hairMelaninParameter(TEXT("hairMelanin"));
	const FName hairRednessParameter(TEXT("hairRedness"));
	const FName irisPrimaryHueParameter(TEXT("Iris Primary Color Hue"));
	const FName irisPrimaryValueParameter(TEXT("Iris Primary Color Value"));
	const FName irisSecondaryHueParameter(TEXT("Iris Secondary Color Hue"));
	const FName irisSecondaryValueParameter(TEXT("Iris Secondary Color Value"));
	const FName irisSaturationParameter(TEXT("Iris Global Saturation"));
	const FName skinValueParameter(TEXT("Basecolor Global Value Post-Bake"));

	// Slots by name rather than by material, since the blueprint swaps in dynamic instances named for nothing.
	const FName leftEyeSlot(TEXT("eyeLeft_shader_shader"));
	const FName rightEyeSlot(TEXT("eyeRight_shader_shader"));
	const FName bodySkinSlot(TEXT("body_shader_shader"));
	const FName faceSkinSlots[] = { FName(TEXT("head_LOD1_shader_shader")), FName(TEXT("head_LOD3_shader_shader")), FName(TEXT("head_LOD57_shader_shader")) };

	/** Lashes stay dark on a blonde head, and fuzz is skin to anybody looking. Brows, beard and moustache follow the hair. */
	const TCHAR* const uncolouredGrooms[] = { TEXT("Eyelashes"), TEXT("Fuzz") };

	/** Only the latest pending request for a component can install its groom and colour. */
	TMap<TWeakObjectPtr<UGroomComponent>, uint64> pendingGrooms;
	uint64 nextGroomRequest = 0;

	/** Held until done, because a released handle is a cancelled load. */
	TSharedPtr<FStreamableHandle> warmed;
	TMap<FString, TSharedPtr<FStreamableHandle>> pendingBodies;

	USkeletalMeshComponent* findMesh(AActor& avatar, const TCHAR* part)
	{
		TArray<USkeletalMeshComponent*> meshes;
		avatar.GetComponents<USkeletalMeshComponent>(meshes);
		for (USkeletalMeshComponent* mesh : meshes)
		{
			if (mesh->GetName().Contains(part))
			{
				return mesh;
			}
		}

		return nullptr;
	}

	/** The body's player, made the first time it is asked for. */
	UFootIkAnimInstance* footIkOf(USkeletalMeshComponent& mesh)
	{
		UFootIkAnimInstance* player = Cast<UFootIkAnimInstance>(mesh.GetAnimInstance());
		if (player == nullptr)
		{
			mesh.SetAnimInstanceClass(UFootIkAnimInstance::StaticClass());
			player = Cast<UFootIkAnimInstance>(mesh.GetAnimInstance());
		}

		return player;
	}

	FString characterFolder(const FString& characterName)
	{
		if (characterName == maleBaseName || characterName == femaleBaseName)
		{
			return FString(TEXT("Humans/Defaults/")) + (characterName == maleBaseName ? TEXT("Male") : TEXT("Female"));
		}

		if (characterName == TEXT("MH_Goblin"))
		{
			return TEXT("Goblins/Male/Goblin");
		}

		const bool elf = characterName.StartsWith(TEXT("MH_Elf"));
		const bool orc = characterName.StartsWith(TEXT("MH_Orc"));
		if (elf || orc)
		{
			const bool male = characterName.Contains(TEXT("Male"), ESearchCase::CaseSensitive);
			const TCHAR form = characterName[characterName.Len() - 1];
			const int32 head = form >= TEXT('B') && form <= TEXT('D') ? form - TEXT('A') : 0;
			const TCHAR* donor = male ? maleHeads[head] : femaleHeads[head];
			return FString::Printf(TEXT("%s/%s/%s"), orc ? TEXT("Orcs") : TEXT("Elves"), male ? TEXT("Male") : TEXT("Female"), donor);
		}

		const FString donor = characterName.RightChop(3);
		for (const TCHAR* maleHead : maleHeads)
		{
			if (donor == maleHead)
			{
				return TEXT("Humans/Male/") + donor;
			}
		}

		return TEXT("Humans/Female/") + donor;
	}

	FString characterPackage(const FString& characterName)
	{
		return FString::Printf(TEXT("%s/%s/BP_%s"), charactersFolder, *characterFolder(characterName), *characterName);
	}

	/** A donor's dwarf body or face package, by the donor's character name, MH_ and its first name. */
	FString dwarfMeshPackage(const FString& characterName, bool face)
	{
		const FString donor = characterName.RightChop(3);
		FString folder = characterFolder(characterName);
		folder.RemoveFromStart(TEXT("Humans/"));
		return face ? FString::Printf(TEXT("%s/%s/Face/SKM_MH_%s_FaceMesh_Preview"), appearanceDwarfFolder, *folder, *donor) : FString::Printf(TEXT("%s/%s/Body/SKM_Dwarf_%s_Body"), appearanceDwarfFolder, *folder, *donor);
	}

	FString dwarfMeshPath(const FString& characterName, bool face)
	{
		const FString package = dwarfMeshPackage(characterName, face);
		return package + TEXT(".") + FPackageName::GetShortName(package);
	}

	/** The picked head when it is installed, the base body otherwise, and empty when neither is: a fresh clone has no MetaHumans at all. */
	FString characterNameFor(const Network::FAppearance& appearance)
	{
		if ((appearance.species != Network::EAppearanceSpecies::Human) && (appearance.species != Network::EAppearanceSpecies::Dwarf))
		{
			static const TCHAR* const suffixes[] = { TEXT(""), TEXT("B"), TEXT("C"), TEXT("D") };
			static_assert(UE_ARRAY_COUNT(suffixes) == AppearanceChoices::heads);
			const uint8 head = appearance.head < AppearanceChoices::heads ? appearance.head : 0;
			const TCHAR* speciesName = appearance.species == Network::EAppearanceSpecies::Orc ? TEXT("MH_Orc") : TEXT("MH_Elf");
			const FString name = FString(speciesName) + (appearance.male ? TEXT("Male") : TEXT("Female")) + suffixes[head];
			return FPackageName::DoesPackageExist(characterPackage(name)) ? name : FString();
		}

		const TCHAR* const* heads = appearance.male ? maleHeads : femaleHeads;
		const FString headName = FString::Printf(TEXT("MH_%s"), heads[(appearance.head < AppearanceChoices::heads) ? appearance.head : 0]);

		// A dwarf is made from one donor, so the base body cannot stand in for it.
		if (appearance.species == Network::EAppearanceSpecies::Dwarf)
		{
			const bool installed = FPackageName::DoesPackageExist(characterPackage(headName)) && FPackageName::DoesPackageExist(dwarfMeshPackage(headName, false)) && FPackageName::DoesPackageExist(dwarfMeshPackage(headName, true));
			return installed ? headName : FString();
		}

		if (FPackageName::DoesPackageExist(characterPackage(headName)))
		{
			return headName;
		}

		const FString baseName = appearance.male ? maleBaseName : femaleBaseName;
		return FPackageName::DoesPackageExist(characterPackage(baseName)) ? baseName : FString();
	}

	FString bodyClassPath(const FString& characterName)
	{
		return FString::Printf(TEXT("%s.BP_%s_C"), *characterPackage(characterName), *characterName);
	}

	/** One component per slot, found by name because a MetaHuman carries six grooms and the first of a class could be any of them. */
	UGroomComponent* findOrCreateGroom(AActor& avatar, USkeletalMeshComponent& face, const TCHAR* slot)
	{
		TArray<UGroomComponent*> grooms;
		avatar.GetComponents<UGroomComponent>(grooms);
		for (UGroomComponent* candidate : grooms)
		{
			if (candidate->GetName() == slot)
			{
				return candidate;
			}
		}

		UGroomComponent* groom = NewObject<UGroomComponent>(&avatar, FName(slot));
		groom->SetupAttachment(&face);
		groom->RegisterComponent();
		return groom;
	}

	void wearLoadedGroom(UGroomComponent& groom, UGroomAsset& groomAsset, UGroomBindingAsset& binding)
	{
		// Simulated strands trail to the ground behind a moving body, so the hair rides the head as bound instead.
		const bool simulated = !groom.SimulationSettings.bOverrideSettings || groom.SimulationSettings.SolverSettings.bEnableSimulation;
		groom.SetVisibility(true);
		if (groom.bUseCards != groom.ComponentHasTag(headwearHairTag))
		{
			groom.SetUseCards(groom.ComponentHasTag(headwearHairTag));
		}
		groom.SimulationSettings.bOverrideSettings = true;
		groom.SimulationSettings.SolverSettings.bEnableSimulation = false;

		// Reassigning rebuilds the groom, and both are set in one call because two calls rebuild twice, the first time on the wrong head.
		if (simulated || (groom.GroomAsset != &groomAsset) || (groom.BindingAsset != &binding))
		{
			groom.SetGroomAsset(&groomAsset, &binding, true);
		}
	}

	void tintGroom(UGroomComponent& groom, const FHairColour& colour);

	/** Fetched rather than loaded where this stands: a first groom cost a third of a second on the game thread, inside the screen's cross fade. */
	void wearGroom(UGroomComponent& groom, const FGroom& chosen, const FString& characterName, const FHairColour& colour)
	{
		const FString groomPath = FString::Printf(TEXT("%s/%s/%s/%s.%s"), groomFolder, chosen.category, chosen.name, chosen.name, chosen.name);
		const FString bindingPath = FString::Printf(TEXT("%s/%s/%s_Binding.%s_Binding"), bindingFolder, *characterFolder(characterName), chosen.name, chosen.name);

		UGroomAsset* groomAsset = FindObject<UGroomAsset>(nullptr, *groomPath);
		UGroomBindingAsset* binding = FindObject<UGroomBindingAsset>(nullptr, *bindingPath);
		if ((groomAsset != nullptr) && (binding != nullptr))
		{
			wearLoadedGroom(groom, *groomAsset, *binding);
			return;
		}

		const TWeakObjectPtr<UGroomComponent> weakGroom(&groom);
		const uint64 request = ++nextGroomRequest;
		pendingGrooms.Add(weakGroom, request);

		TArray<FSoftObjectPath> paths = { FSoftObjectPath(groomPath), FSoftObjectPath(bindingPath) };
		UAssetManager::GetStreamableManager().RequestAsyncLoad(paths, FStreamableDelegate::CreateLambda([weakGroom, groomPath, bindingPath, request, colour]()
		{
			const uint64* pending = pendingGrooms.Find(weakGroom);
			if ((pending == nullptr) || (*pending != request))
			{
				return;
			}

			pendingGrooms.Remove(weakGroom);
			UGroomComponent* component = weakGroom.Get();
			if (component == nullptr)
			{
				return;
			}

			UGroomAsset* loadedGroom = FindObject<UGroomAsset>(nullptr, *groomPath);
			UGroomBindingAsset* loadedBinding = FindObject<UGroomBindingAsset>(nullptr, *bindingPath);
			if ((loadedGroom == nullptr) || (loadedBinding == nullptr))
			{
				UE_LOG(LogTemp, Warning, TEXT("The appearance applier is missing %s, so that groom stays off."), (loadedGroom == nullptr) ? *groomPath : *bindingPath);
				return;
			}

			wearLoadedGroom(*component, *loadedGroom, *loadedBinding);
			tintGroom(*component, colour);
		}));
	}

	/** Puts the chosen groom on a slot, or clears it when the choice is out of range, so a reused component never keeps the last character's hair. */
	void applyGroom(AActor& avatar, USkeletalMeshComponent& face, const TCHAR* slot, TConstArrayView<FGroom> offered, int32 chosen, const FString& characterName, const FHairColour& colour)
	{
		UGroomComponent* groom = findOrCreateGroom(avatar, face, slot);
		pendingGrooms.Remove(groom);
		if (!offered.IsValidIndex(chosen))
		{
			groom->SetVisibility(false);
			if ((groom->GroomAsset != nullptr) || (groom->BindingAsset != nullptr))
			{
				groom->SetGroomAsset(nullptr, nullptr);
			}

			return;
		}

		wearGroom(*groom, offered[chosen], characterName, colour);
	}

	/** Every material slot of a groom gets an instance of its own material, since the component may hand back a substituted default that reads no hair parameter. */
	void tintGroom(UGroomComponent& groom, const FHairColour& colour)
	{
		if (groom.GroomAsset == nullptr)
		{
			return;
		}

		const TArray<FHairGroupsMaterial>& slots = groom.GroomAsset->GetHairGroupsMaterials();
		for (int32 index = 0; index < slots.Num(); index++)
		{
			UMaterialInterface* parent = slots[index].Material;
			if (parent == nullptr)
			{
				continue;
			}

			if (groom.ComponentHasTag(headwearHairTag))
			{
				const FString name = parent->GetName() + TEXT("_Headwear");
				UMaterialInterface* clipped = LoadObject<UMaterialInterface>(nullptr, *(TEXT("/Game/Equipment/Shared/Hair/") + name + TEXT(".") + name));
				if (clipped != nullptr)
				{
					parent = clipped;
				}
			}

			UMaterialInstanceDynamic* dynamic = Cast<UMaterialInstanceDynamic>(groom.GetMaterial(index));
			if ((dynamic == nullptr) || (dynamic->Parent != parent))
			{
				dynamic = UMaterialInstanceDynamic::Create(parent, &groom);
				groom.SetMaterial(index, dynamic);
			}

			dynamic->SetScalarParameterValue(hairMelaninParameter, colour.melanin);
			dynamic->SetScalarParameterValue(hairRednessParameter, colour.redness);
		}
	}

	void applyHairColour(AActor& avatar, const FHairColour& colour)
	{
		TArray<UGroomComponent*> grooms;
		avatar.GetComponents<UGroomComponent>(grooms);
		for (UGroomComponent* groom : grooms)
		{
			bool coloured = true;
			for (const TCHAR* const excluded : uncolouredGrooms)
			{
				coloured = coloured && (groom->GetName() != excluded);
			}

			if (coloured)
			{
				tintGroom(*groom, colour);
			}
		}
	}

	/** Shifts a baked skin slot rather than replacing it, since the baked material carries this character's own skin textures. */
	void tintSkinSlot(USkeletalMeshComponent& mesh, const FName& slot, float tone)
	{
		const int32 index = mesh.GetMaterialIndex(slot);
		if (index == INDEX_NONE)
		{
			return;
		}

		UMaterialInstanceDynamic* dynamic = Cast<UMaterialInstanceDynamic>(mesh.GetMaterial(index));
		if (dynamic == nullptr)
		{
			dynamic = mesh.CreateAndSetMaterialInstanceDynamic(index);
		}

		if (dynamic != nullptr)
		{
			dynamic->SetScalarParameterValue(skinValueParameter, tone);
		}
	}

	void applySkin(AActor& avatar, USkeletalMeshComponent& face, const Network::FAppearance& appearance)
	{
		const float tone = skinTones[(appearance.skinTone < AppearanceChoices::skinTones) ? appearance.skinTone : 0];
		for (const FName& slot : faceSkinSlots)
		{
			tintSkinSlot(face, slot, tone);
		}

		// The body is a separate mesh with its own material, so a face tinted alone would be a mask.
		USkeletalMeshComponent* body = findMesh(avatar, TEXT("Body"));
		if (body != nullptr)
		{
			tintSkinSlot(*body, bodySkinSlot, tone);
		}
	}

	void applyEyes(USkeletalMeshComponent& face, const Network::FAppearance& appearance)
	{
		if (appearance.species != Network::EAppearanceSpecies::Human && appearance.eyeColor == AppearanceChoices::donorEyeColor)
		{
			for (const FName& slot : { leftEyeSlot, rightEyeSlot })
			{
				const int32 index = face.GetMaterialIndex(slot);
				if (index != INDEX_NONE)
				{
					face.SetMaterial(index, nullptr);
				}
			}
			return;
		}

		const FEyeColour& colour = eyeColours[(appearance.eyeColor < AppearanceChoices::eyeColors) ? appearance.eyeColor : 0];
		UMaterialInterface* parametric = LoadObject<UMaterialInterface>(nullptr, parametricEyePath);
		if (parametric == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("The appearance applier could not load %s, so the eyes keep their built colour."), parametricEyePath);
			return;
		}

		for (const FName& slot : { leftEyeSlot, rightEyeSlot })
		{
			const int32 index = face.GetMaterialIndex(slot);
			if (index == INDEX_NONE)
			{
				UE_LOG(LogTemp, Warning, TEXT("The appearance applier found no eye slot %s, so the eyes keep their built colour."), *slot.ToString());
				continue;
			}

			UMaterialInstanceDynamic* dynamic = Cast<UMaterialInstanceDynamic>(face.GetMaterial(index));
			if ((dynamic == nullptr) || (dynamic->Parent != parametric))
			{
				dynamic = UMaterialInstanceDynamic::Create(parametric, &face);
				face.SetMaterial(index, dynamic);
			}

			// Primary and secondary together, or the iris blends two of the six colours at once.
			dynamic->SetScalarParameterValue(irisPrimaryHueParameter, colour.hue);
			dynamic->SetScalarParameterValue(irisPrimaryValueParameter, colour.value);
			dynamic->SetScalarParameterValue(irisSecondaryHueParameter, colour.hue);
			dynamic->SetScalarParameterValue(irisSecondaryValueParameter, colour.value);
			dynamic->SetScalarParameterValue(irisSaturationParameter, colour.saturation);
		}
	}

	/** A preset arrives in a default garment on its own component. This screen makes a person rather than dresses one, so it is hidden. */
	void undress(AActor& avatar)
	{
		TArray<USkeletalMeshComponent*> meshes;
		avatar.GetComponents<USkeletalMeshComponent>(meshes);
		for (USkeletalMeshComponent* mesh : meshes)
		{
			if ((mesh->GetName() != TEXT("Body")) && (mesh->GetName() != TEXT("Face")) && !mesh->ComponentHasTag(starterGarmentTag))
			{
				mesh->SetVisibility(false);
			}
		}
	}

	/** Whether a body wears a dwarf mesh. The same human class stands both as its donor and as the dwarf made from it. */
	bool wearsDwarf(AActor& avatar)
	{
		const USkeletalMeshComponent* body = findMesh(avatar, TEXT("Body"));
		return (body != nullptr) && body->ComponentHasTag(appearanceDwarfTag);
	}

	/**
	 * Stands a human donor as the dwarf made from it: the dwarf body and face, and the roster's proportions.
	 * The body DNA uses the shortened reference skeleton, so its corrective rig preserves volume at bending joints.
	 */
	void shapeDwarf(AActor& avatar, USkeletalMeshComponent& face, const FString& characterName)
	{
		USkeletalMeshComponent* body = findMesh(avatar, TEXT("Body"));
		USkeletalMesh* dwarfBody = LoadObject<USkeletalMesh>(nullptr, *dwarfMeshPath(characterName, false));
		USkeletalMesh* dwarfFace = LoadObject<USkeletalMesh>(nullptr, *dwarfMeshPath(characterName, true));
		if ((body == nullptr) || (dwarfBody == nullptr) || (dwarfFace == nullptr))
		{
			UE_LOG(LogTemp, Warning, TEXT("The appearance applier could not load the dwarf meshes for %s, so it stands as the human."), *characterName);
			return;
		}

		body->SetDisablePostProcessBlueprint(false);
		if (body->GetSkeletalMeshAsset() != dwarfBody)
		{
			body->SetSkeletalMeshAsset(dwarfBody);
		}

		if (face.GetSkeletalMeshAsset() != dwarfFace)
		{
			face.SetSkeletalMeshAsset(dwarfFace);
		}

		avatar.SetActorScale3D(appearanceDwarfScale);
		body->ComponentTags.AddUnique(appearanceDwarfTag);
	}

	void requestNamedBodyClass(const FString& characterName, bool dwarf, TFunction<void(UClass*)> whenReady)
	{
		if (characterName.IsEmpty())
		{
			whenReady(nullptr);
			return;
		}

		// A dwarf's meshes come with its class, so standing one never loads them on the game thread.
		const FString path = bodyClassPath(characterName);
		TArray<FSoftObjectPath> paths = { FSoftObjectPath(path) };
		if (dwarf)
		{
			paths.Add(FSoftObjectPath(dwarfMeshPath(characterName, false)));
			paths.Add(FSoftObjectPath(dwarfMeshPath(characterName, true)));
		}

		bool loaded = true;
		for (const FSoftObjectPath& wanted : paths)
		{
			if (wanted.ResolveObject() == nullptr)
			{
				loaded = false;
				break;
			}
		}

		if (loaded)
		{
			whenReady(FindObject<UClass>(nullptr, *path));
			return;
		}

		// Keyed by what is fetched so two screens asking for the same character share the one fetch.
		const FString key = dwarf ? (path + TEXT("+Dwarf")) : path;
		pendingBodies.FindOrAdd(key) = UAssetManager::GetStreamableManager().RequestAsyncLoad(paths, FStreamableDelegate::CreateLambda([key, path, whenReady]()
		{
			pendingBodies.Remove(key);
			whenReady(FindObject<UClass>(nullptr, *path));
		}));
	}

	/** A body waiting for its frame: the world it stands in and the spawn that stands it. */
	struct FQueuedBody
	{
		TWeakObjectPtr<UWorld> world;
		TFunction<void()> stand;
	};

	/**
	 * One body stands per frame.
	 * A MetaHuman's construction and its two meshes cost about twenty milliseconds of game thread, and the server introduces a whole crowd in one burst, which stood at once froze the frame for a fifth of a second.
	 */
	TArray<FQueuedBody> queuedBodies;

	bool hasQueuedBody(const TWeakObjectPtr<UWorld>& world)
	{
		for (const FQueuedBody& queued : queuedBodies)
		{
			if (queued.world == world)
			{
				return true;
			}
		}

		return false;
	}

	void standQueuedBody(TWeakObjectPtr<UWorld> world);

	/** A timer set for the next tick fires on the next frame even when it is set while timers are firing, so rescheduling from the callback paces one per frame. */
	void scheduleQueuedBody(UWorld& world)
	{
		world.GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateStatic(&standQueuedBody, TWeakObjectPtr<UWorld>(&world)));
	}

	void standQueuedBody(TWeakObjectPtr<UWorld> world)
	{
		for (int32 index = 0; index < queuedBodies.Num(); index++)
		{
			if (queuedBodies[index].world == world)
			{
				const TFunction<void()> stand = MoveTemp(queuedBodies[index].stand);
				queuedBodies.RemoveAt(index);
				stand();
				break;
			}
		}

		if (world.IsValid() && hasQueuedBody(world))
		{
			scheduleQueuedBody(*world.Get());
		}
	}

	void queueBody(UWorld& world, TFunction<void()> stand)
	{
		// A world that ended took its timer with it, so whatever it still had queued goes too.
		for (int32 index = queuedBodies.Num() - 1; index >= 0; index--)
		{
			if (!queuedBodies[index].world.IsValid())
			{
				queuedBodies.RemoveAt(index);
			}
		}

		const TWeakObjectPtr<UWorld> weakWorld(&world);
		const bool idle = !hasQueuedBody(weakWorld);
		queuedBodies.Add({ weakWorld, MoveTemp(stand) });
		if (idle)
		{
			scheduleQueuedBody(world);
		}
	}
}

namespace Appearance
{
	float measureRunSpeed(const UAnimationAsset& clip, const USkeletalMesh& mesh)
	{
		const UAnimSequence* sequence = Cast<UAnimSequence>(&clip);
		const float length = (sequence != nullptr) ? sequence->GetPlayLength() : 0.0f;
		if (length <= 0.0f)
		{
			return 0.0f;
		}

		// A clip that carries its own motion says how fast it goes.
		if (sequence->HasRootMotion())
		{
			return sequence->ExtractRootMotionFromRange(0.0, length, FAnimExtractContext(0.0, true)).GetTranslation().Size2D() / length;
		}

		// Otherwise the planted foot tells: it stands still on the ground while the body goes over it, so it slides back through the body's space at the body's speed.
		// The clip is posed on this body's own bones, because legs of different lengths slide at different speeds.
		const FReferenceSkeleton& skeleton = mesh.GetRefSkeleton();
		TArray<FBoneIndexType> required;
		for (int32 bone = 0; bone < skeleton.GetNum(); bone++)
		{
			required.Add(static_cast<FBoneIndexType>(bone));
		}

		FBoneContainer bones;
		bones.InitializeTo(required, UE::Anim::FCurveFilterSettings(), mesh);

		static const FName footNames[][2] = { { TEXT("foot_l"), TEXT("foot_r") }, { TEXT("ball_l"), TEXT("ball_r") } };
		for (const FName (&feet)[2] : footNames)
		{
			FCompactPoseBoneIndex indices[2];
			for (int32 side = 0; side < 2; side++)
			{
				const int32 meshIndex = skeleton.FindBoneIndex(feet[side]);
				indices[side] = (meshIndex == INDEX_NONE) ? FCompactPoseBoneIndex(INDEX_NONE) : bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(meshIndex));
			}

			if (!indices[0].IsValid() || !indices[1].IsValid())
			{
				continue;
			}

			// Where each foot is in the body's space at every sample.
			TArray<FVector> places[2];
			for (int32 sample = 0; sample < appearanceRunMeasureSamples; sample++)
			{
				// Poses live on the frame's stack, which wants a mark to give back to.
				FMemMark mark(FMemStack::Get());
				FCompactPose pose;
				pose.SetBoneContainer(&bones);
				FBlendedCurve curve;
				curve.InitFrom(bones);
				UE::Anim::FStackAttributeContainer attributes;
				FAnimationPoseData poseData(pose, curve, attributes);
				sequence->GetAnimationPose(poseData, FAnimExtractContext(static_cast<double>(length) * sample / appearanceRunMeasureSamples, false));

				FCSPose<FCompactPose> componentPose;
				componentPose.InitPose(pose);
				for (int32 side = 0; side < 2; side++)
				{
					places[side].Add(componentPose.GetComponentSpaceTransform(indices[side]).GetLocation());
				}
			}

			float total = 0.0f;
			for (const TArray<FVector>& foot : places)
			{
				// A body faces +Y in its own space, so a planted foot slides toward -Y. A foot that swings low past the ground slides the other way and is not counted.
				FBox travel(foot);
				double slide = 0.0;
				int32 steps = 0;
				for (int32 sample = 0; sample + 1 < foot.Num(); sample++)
				{
					const double step = foot[sample + 1].Y - foot[sample].Y;
					if ((step < 0.0) && (foot[sample].Z < travel.Min.Z + appearancePlantedHeightCm) && (foot[sample + 1].Z < travel.Min.Z + appearancePlantedHeightCm))
					{
						slide -= step;
						steps++;
					}
				}

				// Too few planted steps to say anything.
				if (steps < 3)
				{
					return 0.0f;
				}

				total += static_cast<float>(slide / steps * appearanceRunMeasureSamples / length);
			}

			return total / 2.0f;
		}

		return 0.0f;
	}

	namespace
	{
		/**
		 * What a run clip measures on a body, once: the first body to use a clip pays for the measurement, and a clip that cannot be measured is said so a single time.
		 * The measurement is on the mesh's own bones, so a body standing larger in the world, as an orc does, strides that much further.
		 * A body faces +Y in its own space, so its depth scale is the one a stride covers: a dwarf stands broader than it stands deep.
		 */
		float runClipSpeed(const UAnimationAsset& clip, const USkeletalMeshComponent& body)
		{
			static TMap<FObjectKey, TMap<FObjectKey, float>> measured;

			const USkeletalMesh* mesh = body.GetSkeletalMeshAsset();
			if (mesh == nullptr)
			{
				return 0.0f;
			}

			const float scale = static_cast<float>(body.GetComponentScale().Y);
			TMap<FObjectKey, float>& onBodies = measured.FindOrAdd(FObjectKey(&clip));
			if (const float* known = onBodies.Find(FObjectKey(mesh)))
			{
				return *known * scale;
			}

			const float speed = measureRunSpeed(clip, *mesh);
			if (speed < appearanceLeastClipSpeedCm)
			{
				UE_LOG(LogTemp, Warning, TEXT("The appearance applier could not measure the run clip %s on %s, so it plays at its own rate."), *clip.GetName(), *mesh->GetName());
			}

			onBodies.Add(FObjectKey(mesh), speed);
			return speed * scale;
		}
	}

	EMotion pickMovingMotion(TConstArrayView<FMovingClip> clips, float speedCm, EMotion playing)
	{
		// Slip in ratio terms, so a clip twice too fast and one half as fast are equally bad: the body's speed against the one the clip covers at its clamped rate.
		const auto slipOf = [speedCm](const FMovingClip& clip)
		{
			return (clip.speedCm >= appearanceLeastClipSpeedCm) ? static_cast<double>(FMath::Abs(FMath::Loge(FMath::Max(speedCm, 1.0f) / (clip.speedCm * playRate(speedCm, clip.speedCm))))) : MAX_dbl;
		};

		const FMovingClip* best = &clips.Last();
		double bestSlip = MAX_dbl;
		const FMovingClip* current = nullptr;
		for (const FMovingClip& clip : clips)
		{
			const double slip = slipOf(clip);
			if (slip < bestSlip)
			{
				best = &clip;
				bestSlip = slip;
			}

			if (clip.motion == playing)
			{
				current = &clip;
			}
		}

		if ((current != nullptr) && (slipOf(*current) <= bestSlip / appearanceClipSwapSlipShare))
		{
			return current->motion;
		}

		return best->motion;
	}

	float playRate(float speedCm, float clipSpeedCm)
	{
		return (clipSpeedCm >= appearanceLeastClipSpeedCm) ? FMath::Clamp(speedCm / clipSpeedCm, appearanceLeastPlayRate, appearanceGreatestPlayRate) : 1.0f;
	}

	void animate(AActor& body, EMotion motion, float speedCm)
	{
		// The Universal Animation Library clips, baked onto the MetaHuman skeleton with MetaHuman hands and shoulders.
		static const TCHAR* const clips[] =
		{
			TEXT("/Game/Characters/Animations/UAL/Idle_Selection_Loop.Idle_Selection_Loop"),
			TEXT("/Game/Characters/Animations/UAL/Jog_Fwd_Loop.Jog_Fwd_Loop"),
			TEXT("/Game/Characters/Animations/UAL/Jump_Loop.Jump_Loop"),
			TEXT("/Game/Characters/Animations/UAL/Swim_Idle_Loop.Swim_Idle_Loop"),
			TEXT("/Game/Characters/Animations/UAL/Swim_Fwd_Loop.Swim_Fwd_Loop"),
			TEXT("/Game/Characters/Animations/UAL/Goblin/Idle_Selection_Loop_Goblin.Idle_Selection_Loop_Goblin"),
			TEXT("/Game/Characters/Animations/UAL/Goblin/Jog_Fwd_Loop_Goblin.Jog_Fwd_Loop_Goblin"),
			TEXT("/Game/Characters/Animations/UAL/Walk_Loop.Walk_Loop")
		};
		static_assert(UE_ARRAY_COUNT(clips) == static_cast<uint8>(EMotion::Move), "Every motion before Move plays a clip of its own; Move and GoblinMove pick among them.");

		// Looked up once rather than by every creature every frame. A weak pointer, since a clip in use is kept loaded by the body playing it.
		static TWeakObjectPtr<UAnimationAsset> loaded[UE_ARRAY_COUNT(clips)];
		auto clipOf = [](EMotion of) -> UAnimationAsset*
		{
			TWeakObjectPtr<UAnimationAsset>& slot = loaded[static_cast<uint8>(of)];
			if (!slot.IsValid())
			{
				slot = LoadObject<UAnimationAsset>(nullptr, clips[static_cast<uint8>(of)]);
			}

			return slot.Get();
		};

		USkeletalMeshComponent* mesh = findMesh(body, TEXT("Body"));
		if (mesh == nullptr)
		{
			return;
		}

		// Moving over land has no clip of its own: the speed picks among the clips this body moves with, slowest first.
		if ((motion == EMotion::Move) || (motion == EMotion::GoblinMove))
		{
			const UFootIkAnimInstance* playing = Cast<UFootIkAnimInstance>(mesh->GetAnimInstance());
			EMotion playingMotion = motion;
			static const EMotion humanClips[] = { EMotion::Walk, EMotion::Run };
			static const EMotion goblinClips[] = { EMotion::GoblinRun };
			TArray<FMovingClip, TInlineAllocator<2>> candidates;
			for (const EMotion candidate : (motion == EMotion::Move) ? TConstArrayView<EMotion>(humanClips) : TConstArrayView<EMotion>(goblinClips))
			{
				const UAnimationAsset* candidateClip = clipOf(candidate);
				if (candidateClip != nullptr)
				{
					candidates.Add({ candidate, runClipSpeed(*candidateClip, *mesh) });
					if ((playing != nullptr) && (playing->GetAnimationAsset() == candidateClip))
					{
						playingMotion = candidate;
					}
				}
			}

			if (candidates.IsEmpty())
			{
				return;
			}

			motion = pickMovingMotion(candidates, speedCm, playingMotion);
		}

		UAnimationAsset* clip = clipOf(motion);
		if (clip == nullptr)
		{
			return;
		}

		UFootIkAnimInstance* player = footIkOf(*mesh);
		if (player == nullptr)
		{
			return;
		}

		// Standing feet find the ground. A running stride on a slope puts one foot far above the other, and bending to both folds the front leg, so running keeps the clip.
		// So do falling and swimming, and the goblin's clips already meet the ground it tilts to.
		player->grounded = (motion == EMotion::Idle);
		player->plantFeet = (motion == EMotion::Walk) && (mesh->ComponentHasTag(appearanceDwarfTag) || mesh->ComponentHasTag(appearanceOrcTag));
		float lift = 0.0f;
		if (mesh->ComponentHasTag(appearanceDwarfTag))
		{
			lift = (motion == EMotion::Idle) ? appearanceDwarfStandLift : (motion == EMotion::Walk) ? appearanceDwarfWalkLift : (motion == EMotion::Run) ? appearanceDwarfRunLift : 0.0f;
		}
		else if (motion == EMotion::Walk)
		{
			if (mesh->ComponentHasTag(appearanceOrcTag))
			{
				lift = appearanceOrcWalkLift;
			}
		}

		player->lift = lift;
		if (player->GetAnimationAsset() != clip)
		{
			player->SetAnimationAsset(clip, true);
			player->SetPlaying(true);

			// Single-node clip changes jump the pose, so cloth discards its previous collision history.
			TInlineComponentArray<USkeletalMeshComponent*> garments;
			body.GetComponents(garments);
			for (USkeletalMeshComponent* garment : garments)
			{
				if (garment->ComponentHasTag(starterGarmentTag))
				{
					garment->ForceClothNextUpdateTeleportAndReset();
				}
			}
		}

		// The rate changes the pace and never the phase, so it is set on its own and the clip is not started over.
		const bool onLand = (motion == EMotion::Run) || (motion == EMotion::GoblinRun) || (motion == EMotion::Walk);
		const float rate = onLand ? playRate(speedCm, runClipSpeed(*clip, *mesh)) : 1.0f;
		if (!FMath::IsNearlyEqual(player->GetPlayRate(), rate))
		{
			player->SetPlayRate(rate);
		}
	}

	uint8 wornPieces(TConstArrayView<int32> itemIds)
	{
		// In the order of the worn bits: the starter outfit's five garments, then the sword.
		const TCHAR* const visuals[] = { TEXT("Villager.Hat"), TEXT("Villager.Shirt"), TEXT("Villager.Pants"), TEXT("Villager.Gloves"), TEXT("Villager.Boots"), TEXT("Weapon.StarterWoodenSword") };
		uint8 worn = 0;
		for (const int32 itemId : itemIds)
		{
			const FItemData* item = ItemCatalog::find(itemId);
			for (int32 index = 0; (item != nullptr) && (index < UE_ARRAY_COUNT(visuals)); index++)
			{
				if (item->visual == visuals[index])
				{
					worn |= 1 << index;
				}
			}
		}

		return worn;
	}

	bool dressStarter(AActor& avatar, const Network::FAppearance& appearance, const FString& pieceOnly, bool fallback, uint8 worn)
	{
		USkeletalMeshComponent* body = findMesh(avatar, TEXT("Body"));
		if (body == nullptr)
		{
			return false;
		}

		USkeletalMesh* actualBody = body->GetSkeletalMeshAsset();
		if (actualBody == nullptr)
		{
			return false;
		}

		FString race;
		switch (appearance.species)
		{
			case Network::EAppearanceSpecies::Human:
			{
				race = TEXT("Human");
				break;
			}
			case Network::EAppearanceSpecies::Elf:
			{
				race = TEXT("Elf");
				break;
			}
			case Network::EAppearanceSpecies::Orc:
			{
				race = TEXT("Orc");
				break;
			}
			case Network::EAppearanceSpecies::Dwarf:
			{
				race = TEXT("Dwarf");
				break;
			}
			default:
			{
				return false;
			}
		}

		const TCHAR* const names[] = { TEXT("Hat"), TEXT("Shirt"), TEXT("Pants"), TEXT("Gloves"), TEXT("Boots"), TEXT("BreastWrap"), TEXT("Braies") };
		const bool pieceFiltered = !pieceOnly.IsEmpty();
		bool validPiece = !pieceFiltered;
		for (const TCHAR* name : names)
		{
			validPiece |= pieceOnly == name;
		}

		if (!validPiece || (appearance.male && pieceOnly == TEXT("BreastWrap")))
		{
			return false;
		}

		const uint8 head = appearance.head < AppearanceChoices::heads ? appearance.head : 0;
		const FString headDonor = appearance.male ? maleHeads[head] : femaleHeads[head];
		FString donor = appearance.male ? TEXT("Victor") : TEXT("Jelena");
		if (appearance.species == Network::EAppearanceSpecies::Dwarf)
		{
			donor = headDonor;
		}
		else if (appearance.species == Network::EAppearanceSpecies::Orc)
		{
			if (appearance.male && head > 0)
			{
				donor = maleHeads[1];
			}
		}

		const FString folder = TEXT("/Game/Equipment/Clothing/Villager/Rags/") + race + TEXT("/");
		const FReferenceSkeleton& bodyReference = actualBody->GetRefSkeleton();
		const int32 bodyBoneCount = bodyReference.GetNum();
		const TArray<FTransform>& bodyRefPose = bodyReference.GetRefBonePose();
		USkeletalMesh* pieces[UE_ARRAY_COUNT(names)] = {};
		bool complete = true;
		for (int32 index = 0; index < UE_ARRAY_COUNT(names); index++)
		{
			if ((appearance.male && index == 5) || (pieceFiltered && pieceOnly != names[index]))
			{
				continue;
			}

			const TCHAR* meshPiece = index == 0 ? TEXT("Hat_Cowboy") : index == 5 ? TEXT("CroppedTop") : index == 6 ? TEXT("Pants") : names[index];
			const FString name = FString(TEXT("SKM_Villager_")) + meshPiece;
			const FString fitDonor = index == 0 ? headDonor : donor;
			const FString path = folder + fitDonor + TEXT("/") + name + TEXT(".") + name;
			USkeletalMesh* piece = LoadObject<USkeletalMesh>(nullptr, *path);
			if (piece == nullptr)
			{
				complete = false;
				UE_LOG(LogTemp, Warning, TEXT("The starter garment is missing: %s"), *path);
				continue;
			}

			const FReferenceSkeleton& reference = piece->GetRefSkeleton();
			const int32 boneCount = reference.GetNum();
			const TArray<FTransform>& refPose = reference.GetRefBonePose();
			bool compatible = boneCount == bodyBoneCount;
			for (int32 boneIndex = 0; compatible && boneIndex < boneCount; boneIndex++)
			{
				const int32 bodyIndex = bodyReference.FindBoneIndex(reference.GetBoneName(boneIndex));
				if (bodyIndex == INDEX_NONE)
				{
					compatible = false;
					break;
				}

				compatible = refPose[boneIndex].Equals(bodyRefPose[bodyIndex], 0.001);
				const int32 parent = reference.GetParentIndex(boneIndex);
				const int32 bodyParent = bodyReference.GetParentIndex(bodyIndex);
				compatible &= (parent == INDEX_NONE) == (bodyParent == INDEX_NONE);
				if (parent != INDEX_NONE && bodyParent != INDEX_NONE)
				{
					compatible &= reference.GetBoneName(parent) == bodyReference.GetBoneName(bodyParent);
				}
			}

			if (!compatible)
			{
				complete = false;
				UE_LOG(LogTemp, Warning, TEXT("The starter garment %s does not match the actual body %s."), *path, *actualBody->GetPathName());
				continue;
			}

			pieces[index] = piece;
		}

		bool glovesVisible = false;
		bool hatVisible = false;
		bool legLayerVisible = false;
		bool torsoLayerVisible = false;
		for (int32 index = 0; index < UE_ARRAY_COUNT(names); index++)
		{
			const FName name(*(FString(TEXT("SKM_Villager_")) + names[index]));
			USkeletalMeshComponent* garment = FindObjectFast<USkeletalMeshComponent>(&avatar, name);
			if (pieces[index] == nullptr)
			{
				if (garment != nullptr && garment->ComponentHasTag(starterGarmentTag))
				{
					garment->SetVisibility(false);
					garment->bDisableClothSimulation = true;
					garment->SetComponentTickEnabled(false);
				}
				continue;
			}

			if (garment == nullptr)
			{
				garment = NewObject<USkeletalMeshComponent>(&avatar, name);
				avatar.AddInstanceComponent(garment);
				garment->ComponentTags.Add(starterGarmentTag);
				garment->SetupAttachment(body);
			}

			if (!garment->ComponentHasTag(starterGarmentTag))
			{
				complete = false;
				continue;
			}

			// The underwear shows wherever the shirt or the pants is not worn, so taking one off never leaves the body bare.
			const bool shirtShown = !fallback && (pieces[1] != nullptr) && ((worn & wornShirt) != 0);
			const bool pantsShown = !fallback && (pieces[2] != nullptr) && ((worn & wornPants) != 0);
			const bool visible = pieceFiltered || (index < 5 ? !fallback && ((worn & (1 << index)) != 0) : index == 5 ? !shirtShown : !pantsShown);
			if (garment->GetSkeletalMeshAsset() != pieces[index])
			{
				garment->SetSkeletalMeshAsset(pieces[index]);
			}

			if (index >= 5)
			{
				for (int32 slot = 0; slot < garment->GetNumMaterials(); slot++)
				{
					UMaterialInstanceDynamic* linen = garment->CreateDynamicMaterialInstance(slot);
					if (linen != nullptr)
					{
						for (const FName parameter : { FName(TEXT("C_color")), FName(TEXT("diffuse_color_1")), FName(TEXT("diffuse_color_2")), FName(TEXT("B_diffuse_color_1")) })
						{
							linen->SetVectorParameterValue(parameter, FLinearColor(0.38f, 0.315f, 0.23f));
						}
					}
				}
			}

			garment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			garment->SetLeaderPoseComponent(body);
			garment->UnbindClothFromLeaderPoseComponent();
			garment->SetAllowClothActors(true);
			garment->bDisableClothSimulation = !visible;
			garment->ClothBlendWeight = 1.0f;
			garment->bUseBoundsFromLeaderPoseComponent = true;
			garment->SetVisibility(visible);
			garment->SetComponentTickEnabled(visible);
			if (!garment->IsRegistered())
			{
				garment->RegisterComponent();
			}

			hatVisible |= visible && index == 0;
			legLayerVisible |= visible && (index == 2 || index == 6);
			torsoLayerVisible |= visible && index == 1;
			glovesVisible |= visible && index == 3;
		}

		UGroomComponent* hair = FindObjectFast<UGroomComponent>(&avatar, TEXT("Hair"));
		if (hair != nullptr)
		{
			if (hatVisible)
			{
				hair->ComponentTags.AddUnique(headwearHairTag);
			}
			else
			{
				hair->ComponentTags.Remove(headwearHairTag);
			}

			if (hair->bUseCards != hatVisible)
			{
				hair->SetUseCards(hatVisible);
			}
			hair->SetVisibility(hair->GroomAsset != nullptr);
			tintGroom(*hair, hairColours[appearance.hairColor < AppearanceChoices::hairColors ? appearance.hairColor : 0]);
			UHeadwearHairComponent* clipping = FindObjectFast<UHeadwearHairComponent>(&avatar, TEXT("HeadwearHair"));
			if (clipping == nullptr)
			{
				clipping = NewObject<UHeadwearHairComponent>(&avatar, TEXT("HeadwearHair"));
				avatar.AddInstanceComponent(clipping);
				clipping->RegisterComponent();
			}

			clipping->configure(hair, body, hatVisible ? FindObjectFast<USkeletalMeshComponent>(&avatar, TEXT("SKM_Villager_Hat")) : nullptr);
		}

		const FName weaponName(TEXT("StarterWoodenSword"));
		const FName carryBone(TEXT("pelvis"));
		UStaticMeshComponent* weapon = FindObjectFast<UStaticMeshComponent>(&avatar, weaponName);
		UStaticMesh* sword = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Equipment/Weapons/StarterWoodenSword/SM_StarterWoodenSword.SM_StarterWoodenSword"));
		if (sword == nullptr || !body->DoesSocketExist(carryBone) || !body->DoesSocketExist(TEXT("thigh_l")) || !body->DoesSocketExist(TEXT("thigh_r")))
		{
			complete = false;
			if (weapon != nullptr)
			{
				weapon->SetVisibility(false);
			}
		}
		else
		{
			if (weapon == nullptr)
			{
				weapon = NewObject<UStaticMeshComponent>(&avatar, weaponName);
				avatar.AddInstanceComponent(weapon);
				weapon->SetupAttachment(body, carryBone);
			}

			const FTransform carryReference(actualBody->GetComposedRefPoseMatrix(carryBone));
			const FVector leftHip = actualBody->GetComposedRefPoseMatrix(FName(TEXT("thigh_l"))).GetOrigin();
			const FVector rightHip = actualBody->GetComposedRefPoseMatrix(FName(TEXT("thigh_r"))).GetOrigin();
			const FVector hipSpan = leftHip - rightHip;
			const FVector left = FVector(hipSpan.X, hipSpan.Y, 0.0).GetSafeNormal();
			const FVector forward = FVector::CrossProduct(FVector::UpVector, left);
			const FVector bladeDirection = (-FVector::UpVector - forward * 0.28).GetSafeNormal();
			const FVector grip = leftHip + FVector::UpVector * 12.0 + left * 14.0 + forward * 2.0;
			const FTransform carry(FRotationMatrix::MakeFromZX(bladeDirection, forward).ToQuat(), grip);
			if (weapon->GetAttachParent() != body || weapon->GetAttachSocketName() != carryBone)
			{
				weapon->AttachToComponent(body, FAttachmentTransformRules::KeepRelativeTransform, carryBone);
			}

			weapon->SetStaticMesh(sword);
			weapon->SetAbsolute(false, false, true);
			weapon->SetRelativeTransform(carry.GetRelativeTransform(carryReference));
			weapon->SetWorldScale3D(FVector(heightScale(avatar)));
			weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			weapon->SetVisibility(!pieceFiltered && !fallback && ((worn & wornSword) != 0));
			if (!weapon->IsRegistered())
			{
				weapon->RegisterComponent();
			}
		}

		const FString profileName = TEXT("MI_Coverage_") + race + TEXT("_") + donor;
		const FString profilePath = TEXT("/Game/Equipment/Shared/Coverage/") + profileName + TEXT(".") + profileName;
		UMaterialInterface* profile = LoadObject<UMaterialInterface>(nullptr, *profilePath);
		const int32 bodySlot = body->GetMaterialIndex(bodySkinSlot);
		if (profile == nullptr || bodySlot == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("The starter coverage profile or body skin slot is missing: %s"), *profilePath);
			return false;
		}

		UMaterialInstanceDynamic* coverage = Cast<UMaterialInstanceDynamic>(body->GetMaterial(bodySlot));
		if (coverage == nullptr || coverage->Parent != profile)
		{
			coverage = UMaterialInstanceDynamic::Create(profile, &avatar);
			coverage->CopyMaterialUniformParameters(body->GetMaterial(bodySlot));
			body->SetMaterial(bodySlot, coverage);
		}

		for (const FName parameter : { FName(TEXT("CoverageFootZ")), FName(TEXT("CoverageHandCutoff")), FName(TEXT("CoverageHandRadius")), FName(TEXT("CoveragePantsMinZ")), FName(TEXT("CoveragePantsMaxZ")), FName(TEXT("CoveragePantsRadiusX")) })
		{
			float value = 0.0f;
			if (profile->GetScalarParameterValue(parameter, value))
			{
				coverage->SetScalarParameterValue(parameter, value);
			}
		}

		for (const FName parameter : { FName(TEXT("CoverageWristL")), FName(TEXT("CoverageWristR")), FName(TEXT("CoverageAxisL")), FName(TEXT("CoverageAxisR")) })
		{
			FLinearColor value;
			if (profile->GetVectorParameterValue(parameter, value))
			{
				coverage->SetVectorParameterValue(parameter, value);
			}
		}

		coverage->SetScalarParameterValue(TEXT("FootCoverage"), 0.0f);
		coverage->SetScalarParameterValue(TEXT("CoverageHandCutoff"), -0.5f);
		USkeletalMesh* legLayer = pieces[2] != nullptr ? pieces[2] : pieces[6];
		USkeletalMesh* torsoLayer = pieces[1] != nullptr ? pieces[1] : pieces[5];
		USkeletalMesh* lowestLayer = legLayerVisible ? legLayer : torsoLayer;
		if (lowestLayer != nullptr)
		{
			const float minimumZ = lowestLayer->GetImportedBounds().GetBox().Min.Z + 2.0f;
			const FName upperBone = torsoLayerVisible ? FName(TEXT("spine_05")) : FName(TEXT("spine_01"));
			const float maximumZ = actualBody->GetComposedRefPoseMatrix(upperBone).GetOrigin().Z - 2.0f;
			const float torsoRadius = FMath::Abs(actualBody->GetComposedRefPoseMatrix(FName(TEXT("upperarm_l"))).GetOrigin().X);
			const float hipRadius = FMath::Abs(actualBody->GetComposedRefPoseMatrix(FName(TEXT("thigh_l"))).GetOrigin().X) + 8.0f;
			coverage->SetScalarParameterValue(TEXT("CoveragePantsMinZ"), minimumZ);
			coverage->SetScalarParameterValue(TEXT("CoveragePantsMaxZ"), maximumZ);
			coverage->SetScalarParameterValue(TEXT("CoveragePantsRadiusX"), FMath::Max(torsoRadius, hipRadius));
		}

		coverage->SetScalarParameterValue(TEXT("HandCoverage"), glovesVisible ? 1.0f : 0.0f);
		coverage->SetScalarParameterValue(TEXT("PantsCoverage"), legLayerVisible || torsoLayerVisible ? 1.0f : 0.0f);
		return complete;
	}

	void apply(AActor& avatar, const Network::FAppearance& appearance, uint8 worn)
	{
		USkeletalMeshComponent* face = findMesh(avatar, TEXT("Face"));
		const FString characterName = characterNameFor(appearance);
		if ((face == nullptr) || characterName.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("The appearance applier found no MetaHuman face on %s, so nothing was applied."), *avatar.GetName());
			return;
		}

		undress(avatar);

		// Before the skin and the grooms, which dress whichever meshes the body wears.
		if (appearance.species == Network::EAppearanceSpecies::Dwarf)
		{
			shapeDwarf(avatar, *face, characterName);
		}

		// The body's own proportions play on every clip, so its player keeps them.
		// A dwarf stands on its standing lift until a clip asks for another.
		USkeletalMeshComponent* body = findMesh(avatar, TEXT("Body"));
		UFootIkAnimInstance* player = (body != nullptr) ? footIkOf(*body) : nullptr;
		if (player != nullptr)
		{
			player->lift = wearsDwarf(avatar) ? appearanceDwarfStandLift : 0.0f;
			player->plantFeet = false;
		}

		if (appearance.species == Network::EAppearanceSpecies::Orc)
		{
			face->SetDisablePostProcessBlueprint(true);
			if (body != nullptr)
			{
				body->ComponentTags.AddUnique(appearanceOrcTag);
			}
		}
		else if (body != nullptr)
		{
			body->ComponentTags.Remove(appearanceOrcTag);
		}

		applySkin(avatar, *face, appearance);
		applyEyes(*face, appearance);

		const FHairColour& colour = hairColours[(appearance.hairColor < AppearanceChoices::hairColors) ? appearance.hairColor : 0];
		const int32 hairStyle = (appearance.hairStyle == AppearanceChoices::baldHairStyle) ? INDEX_NONE : appearance.hairStyle;
		applyGroom(avatar, *face, TEXT("Hair"), hairStyles, hairStyle, characterName, colour);
		applyGroom(avatar, *face, TEXT("Eyebrows"), eyebrowStyles, appearance.eyebrows, characterName, colour);

		// Zero is bare, so the byte is one ahead of the list, and only men wear either, whatever a byte says.
		applyGroom(avatar, *face, TEXT("Beard"), beards, appearance.male ? (appearance.beard - 1) : -1, characterName, colour);
		applyGroom(avatar, *face, TEXT("Mustache"), mustaches, appearance.male ? (appearance.mustache - 1) : -1, characterName, colour);

		// After the grooms, since a groom just swapped brings its own materials with it.
		applyHairColour(avatar, colour);
		FString pieceOnly;
		bool fallback = false;
#if WITH_DEV_AUTOMATION_TESTS
		if (FParse::Param(FCommandLine::Get(), TEXT("WardrobePreview")))
		{
			FParse::Value(FCommandLine::Get(), TEXT("WardrobePiece="), pieceOnly);
			fallback = FParse::Param(FCommandLine::Get(), TEXT("WardrobeFallback"));
		}
#endif
		if (!dressStarter(avatar, appearance, pieceOnly, fallback, worn))
		{
			UE_LOG(LogTemp, Warning, TEXT("The starter outfit could not be fully applied to %s."), *avatar.GetName());
		}
	}

	void warm()
	{
		TArray<FSoftObjectPath> paths = { FSoftObjectPath(parametricEyePath) };
		for (const TCHAR* const baseName : { femaleBaseName, maleBaseName })
		{
			if (FPackageName::DoesPackageExist(characterPackage(baseName)))
			{
				paths.Add(FSoftObjectPath(bodyClassPath(baseName)));
			}
		}

		warmed = UAssetManager::GetStreamableManager().RequestAsyncLoad(paths);
	}

	void requestBodyClass(const Network::FAppearance& appearance, TFunction<void(UClass*)> whenReady)
	{
		requestNamedBodyClass(characterNameFor(appearance), appearance.species == Network::EAppearanceSpecies::Dwarf, MoveTemp(whenReady));
	}

	float heightScale(AActor& body)
	{
		if (wearsDwarf(body))
		{
			return appearanceDwarfHeightRatio;
		}

		const USceneComponent* root = body.GetRootComponent();
		return (root != nullptr) ? static_cast<float>(root->GetRelativeScale3D().Z) : 1.0f;
	}

	bool sameShape(AActor& body, const Network::FAppearance& appearance)
	{
		return wearsDwarf(body) == (appearance.species == Network::EAppearanceSpecies::Dwarf);
	}

	void attachTo(AActor& host, const Network::FAppearance& appearance, const FVector& feetOffset, TFunction<void(AActor*)> whenAttached, const FString& authoredBody, uint8 worn)
	{
		// A MetaHuman's mesh sits a quarter turn off its actor's yaw, the same offset the front end's spots carry.
		static constexpr float bodyYawOffset = -90.0f;

		const TWeakObjectPtr<AActor> weakHost(&host);
		const bool useAuthoredBody = !authoredBody.IsEmpty();
		if (useAuthoredBody && !FPackageName::DoesPackageExist(characterPackage(authoredBody)))
		{
			whenAttached(nullptr);
			return;
		}

		const FString characterName = useAuthoredBody ? authoredBody : characterNameFor(appearance);
		const bool dwarf = !useAuthoredBody && (appearance.species == Network::EAppearanceSpecies::Dwarf);
		requestNamedBodyClass(characterName, dwarf, [weakHost, appearance, feetOffset, whenAttached, useAuthoredBody, worn](UClass* bodyClass)
		{
			AActor* hostActor = weakHost.Get();
			if (hostActor == nullptr)
			{
				return;
			}

			UWorld* world = hostActor->GetWorld();
			if ((bodyClass == nullptr) || (world == nullptr))
			{
				whenAttached(nullptr);
				return;
			}

			const TWeakObjectPtr<UClass> weakClass(bodyClass);
			queueBody(*world, [weakHost, weakClass, appearance, feetOffset, whenAttached, useAuthoredBody, worn]()
			{
				// The host can leave, and the class be collected, while the body waits for its frame.
				AActor* queuedHost = weakHost.Get();
				if (queuedHost == nullptr)
				{
					return;
				}

				UClass* queuedClass = weakClass.Get();
				UWorld* queuedWorld = queuedHost->GetWorld();
				if ((queuedClass == nullptr) || (queuedWorld == nullptr))
				{
					whenAttached(nullptr);
					return;
				}

				FActorSpawnParameters parameters;
				parameters.Owner = queuedHost;
				parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AActor* body = queuedWorld->SpawnActor<AActor>(queuedClass, queuedHost->GetActorTransform(), parameters);
				if (body == nullptr)
				{
					whenAttached(nullptr);
					return;
				}

				body->SetActorEnableCollision(false);
				body->AttachToActor(queuedHost, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
				body->SetActorRelativeLocation(feetOffset);
				body->SetActorRelativeRotation(FRotator(0.0f, bodyYawOffset, 0.0f));
				if (!useAuthoredBody)
				{
					apply(*body, appearance, worn);
				}

				whenAttached(body);
			});
		});
	}
}
