#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSingleNodeInstance.h"
#include "Appearance.h"
#include "Appearance/AppearanceApplier.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "DNA.h"
#include "DNAAssetUserData.h"
#include "DNAUtils.h"
#include "SharedRigRuntimeContext.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "dna/BinaryStreamWriter.h"
#include "trio/streams/MemoryStream.h"
#include "pma/ScopedPtr.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "GroomComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Tickable.h"
#include "World/ProxyCharacter.h"
#include "World/WorldCharacter.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDwarfAppearanceTest, "Alverion.Characters.DwarfAppearance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	const TCHAR* const dwarfTestDonors[][4] = { { TEXT("Jelena"), TEXT("Vivian"), TEXT("Aera"), TEXT("Ada") }, { TEXT("Victor"), TEXT("Orlando"), TEXT("Kelvin"), TEXT("Cameron") } };

	/** Includes every rendered position, tangent frame, UV, weight and local reference bone. */
	FString dwarfTestGeometryFingerprint(const USkeletalMeshComponent& body)
	{
		FSHA1 hash;
		const FReferenceSkeleton& reference = body.GetSkeletalMeshAsset()->GetRefSkeleton();
		for (int32 bone = 0; bone < reference.GetNum(); bone++)
		{
			const FString name = reference.GetBoneName(bone).ToString();
			const FVector translation = reference.GetRefBonePose()[bone].GetTranslation();
			const FQuat rotation = reference.GetRefBonePose()[bone].GetRotation();
			const FVector scale = reference.GetRefBonePose()[bone].GetScale3D();
			hash.Update(reinterpret_cast<const uint8*>(*name), name.Len() * sizeof(TCHAR));
			hash.Update(reinterpret_cast<const uint8*>(&translation), sizeof(translation));
			hash.Update(reinterpret_cast<const uint8*>(&rotation), sizeof(rotation));
			hash.Update(reinterpret_cast<const uint8*>(&scale), sizeof(scale));
		}

		for (const FSkeletalMeshLODRenderData& lod : body.GetSkeletalMeshRenderData()->LODRenderData)
		{
			const FPositionVertexBuffer& positions = lod.StaticVertexBuffers.PositionVertexBuffer;
			const FStaticMeshVertexBuffer& frames = lod.StaticVertexBuffers.StaticMeshVertexBuffer;
			const FSkinWeightVertexBuffer& weights = lod.SkinWeightVertexBuffer;
			hash.Update(static_cast<const uint8*>(positions.GetVertexData()), positions.GetNumVertices() * sizeof(FVector3f));
			hash.Update(static_cast<const uint8*>(frames.GetTangentData()), frames.GetTangentSize());
			hash.Update(static_cast<const uint8*>(frames.GetTexCoordData()), frames.GetTexCoordSize());
			hash.Update(weights.GetDataVertexBuffer()->GetWeightData(), weights.GetDataVertexBuffer()->GetVertexDataSize());
			hash.Update(weights.GetLookupVertexBuffer()->GetLookupData(), weights.GetLookupVertexBuffer()->GetVertexDataSize());
			for (const FSkelMeshRenderSection& section : lod.RenderSections)
			{
				hash.Update(reinterpret_cast<const uint8*>(section.BoneMap.GetData()), section.BoneMap.Num() * sizeof(FBoneIndexType));
			}
		}

		hash.Final();
		FSHAHash result;
		hash.GetHash(result.Hash);
		return result.ToString();
	}

	/** The one body a host wears, once its meshes have landed, its frame has come and its grooms have landed. */
	AActor* dwarfTestBodyOf(UWorld& world, AActor& host)
	{
		// A fetch's callback waits for the next tick of the tickables, and the body for the next frame after that.
		FlushAsyncLoading();
		FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.016f);
		GFrameCounter++;
		world.Tick(LEVELTICK_All, 1.0f / 60.0f);
		FlushAsyncLoading();
		FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.016f);
		TArray<AActor*> bodies;
		host.GetAttachedActors(bodies);
		return (bodies.Num() == 1) ? bodies[0] : nullptr;
	}

	AProxyCharacter* dwarfTestProxy(UWorld& world, const Network::FAppearance& look, const FVector& location)
	{
		AProxyCharacter* proxy = world.SpawnActor<AProxyCharacter>();
		Network::FCharacterSpawn spawn;
		spawn.location = location;
		spawn.appearance = look.pack();
		proxy->setup(spawn);
		return proxy;
	}

	/** The lowest point of the skin and the height of the head, both above the body's own origin, which is where its feet stand. */
	void dwarfTestMeasure(USkeletalMeshComponent& mesh, const AActor& body, double& sole, double& head)
	{
		mesh.RefreshBoneTransforms();
		TArray<FMatrix44f> matrices;
		TArray<FVector3f> positions;
		mesh.GetCurrentRefToLocalMatrices(matrices, 0);
		USkinnedMeshComponent::ComputeSkinnedPositions(&mesh, positions, matrices, mesh.GetSkeletalMeshRenderData()->LODRenderData[0], *mesh.GetSkinWeightBuffer(0));
		const FTransform& transform = mesh.GetComponentTransform();
		const double ground = body.GetActorLocation().Z;
		sole = MAX_dbl;
		for (const FVector3f& position : positions)
		{
			sole = FMath::Min(sole, transform.TransformPosition(FVector(position)).Z - ground);
		}

		head = mesh.GetSocketLocation(TEXT("head")).Z - ground;
	}
}

void FDwarfAppearanceTest::GetTests(TArray<FString>& names, TArray<FString>& commands) const
{
	for (const TCHAR* sex : { TEXT("Female"), TEXT("Male") })
	{
		for (TCHAR form = TEXT('A'); form <= TEXT('D'); ++form)
		{
			const FString name = FString::Printf(TEXT("%s%c"), sex, form);
			names.Add(name);
			commands.Add(name);
		}
	}
}

bool FDwarfAppearanceTest::RunTest(const FString& parameters)
{
	Network::FAppearance look;
	look.species = Network::EAppearanceSpecies::Dwarf;
	look.male = parameters.StartsWith(TEXT("Male"));
	const TCHAR* sex = look.male ? TEXT("Male") : TEXT("Female");
	look.head = static_cast<uint8>(parameters[parameters.Len() - 1] - TEXT('A'));
	const FString donor = dwarfTestDonors[look.male ? 1 : 0][look.head];
	const FString donorClassPath = FString::Printf(TEXT("/Game/Characters/MetaHumans/Humans/%s/%s/BP_MH_%s.BP_MH_%s_C"), sex, *donor, *donor, *donor);
	UClass* donorClass = LoadClass<AActor>(nullptr, *donorClassPath);
	if (!TestNotNull(*donor, donorClass))
	{
		return false;
	}

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

	// Another player seen in the world.
	AProxyCharacter* proxy = dwarfTestProxy(*world, look, FVector(100.0, 200.0, 300.0));
	AActor* dwarf = dwarfTestBodyOf(*world, *proxy);
	if (!TestNotNull(TEXT("The dwarf stands one body"), dwarf))
	{
		return false;
	}

	TestEqual(TEXT("A dwarf is its donor's class"), dwarf->GetClass(), donorClass);
	USkeletalMeshComponent* body = FindObjectFast<USkeletalMeshComponent>(dwarf, TEXT("Body"));
	USkeletalMeshComponent* face = FindObjectFast<USkeletalMeshComponent>(dwarf, TEXT("Face"));
	if (!TestNotNull(TEXT("The dwarf has a body"), body) || !TestNotNull(TEXT("The dwarf has a face"), face) || !TestNotNull(TEXT("The body has a mesh"), body->GetSkeletalMeshAsset()) || !TestNotNull(TEXT("The face has a mesh"), face->GetSkeletalMeshAsset()))
	{
		return false;
	}

	const FString originalGeometry = dwarfTestGeometryFingerprint(*body);
	AddInfo(FString::Printf(TEXT("DWARF_GEOMETRY %s %s."), *donor, *originalGeometry));

#if WITH_EDITOR
	if (FParse::Param(FCommandLine::Get(), TEXT("DwarfBakeCalibration")))
	{
		USkeletalMesh* mesh = body->GetSkeletalMeshAsset();
		const UDNAAssetUserData* sourceData = mesh->GetAssetUserData<UDNAAssetUserData>();
		if (!TestNotNull(TEXT("The dwarf body has DNA userdata"), sourceData) || !TestNotNull(TEXT("The dwarf body has source DNA"), sourceData->DNAAsset.Get()))
		{
			return false;
		}

		UDNA* source = LoadObject<UDNA>(nullptr, *FString::Printf(TEXT("/Game/Characters/MetaHumans/Humans/%s/%s/Body/SKM_MH_%s_BodyMesh_DNA.SKM_MH_%s_BodyMesh_DNA"), sex, *donor, *donor, *donor));
		if (!TestNotNull(TEXT("The canonical human body DNA loads"), source))
		{
			return false;
		}
		const TSharedPtr<FSharedRigRuntimeContext> context = source->GetRigRuntimeContext();
		const TSharedPtr<IDNAReader> reader = context->DNAReader;
		const FReferenceSkeleton& reference = mesh->GetRefSkeleton();
		const uint16 count = reader->GetJointCount();
		TArray<dna::Vector3> translations;
		for (uint16 joint = 0; joint < count; joint++)
		{
			const FName name(*reader->GetJointName(joint));
			const int32 index = reference.FindBoneIndex(name);
			if (!TestTrue(*FString::Printf(TEXT("DNA joint %s exists in the dwarf"), *name.ToString()), index != INDEX_NONE))
			{
				return false;
			}

			const int32 parent = reference.GetParentIndex(index);
			const uint16 dnaParent = reader->GetJointParentIndex(joint);
			const bool matchingParent = parent == INDEX_NONE ? dnaParent == joint : reference.GetBoneName(parent) == FName(*reader->GetJointName(dnaParent));
			if (!TestTrue(TEXT("The DNA and mesh retain the same joint hierarchy"), matchingParent))
			{
				return false;
			}

			const FVector location = reference.GetRefBonePose()[index].GetTranslation();
			translations.Add({ static_cast<float>(location.X), static_cast<float>(location.Y), static_cast<float>(location.Z) });
		}

		pma::ScopedPtr<trio::MemoryStream> stream = pma::makeScoped<trio::MemoryStream>();
		pma::ScopedPtr<dna::BinaryStreamWriter> writer = pma::makeScoped<dna::BinaryStreamWriter>(stream.get());
		writer->setFrom(reader->Unwrap(), CalculateDNADataLayerBitmask(EDNADataLayer::RBFBehavior | EDNADataLayer::MachineLearnedBehavior));
		writer->setNeutralJointTranslations(translations.GetData(), count);
		writer->write();
		stream->seek(0);
		FDNAConfig config = reader->GetConfig();
		config.CoordinateSystemTransformPolicy = ECoordinateSystemTransformPolicy::Preserve;
		const TSharedPtr<IDNAReader> calibratedReader = LoadDNAFromStream(stream.get(), config);
		if (!TestTrue(TEXT("The calibrated DNA reader loads"), calibratedReader.IsValid()))
		{
			return false;
		}

		const FString packageName = FString::Printf(TEXT("/Game/Characters/MetaHumans/Dwarfs/%s/%s/Body/DNA_Dwarf_%s_Body"), sex, *donor, *donor);
		const FString objectName = FString::Printf(TEXT("DNA_Dwarf_%s_Body"), *donor);
		UPackage* package = FPackageName::DoesPackageExist(packageName) ? LoadPackage(nullptr, *packageName, LOAD_None) : CreatePackage(*packageName);
		package->FullyLoad();
		UDNA* calibrated = FindObject<UDNA>(package, *objectName);
		if (calibrated == nullptr)
		{
			calibrated = NewObject<UDNA>(package, *objectName, RF_Public | RF_Standalone);
		}

		if (calibrated != source)
		{
			calibrated->InitializeForRuntimeFrom(source);
			calibrated->RigLogicConfiguration = source->RigLogicConfiguration;
		}
		calibrated->SetDNAReader(calibratedReader);
		const TArrayView<const float> oldNeutral = context->RigLogic->GetNeutralJointValues();
		const TArrayView<const float> newNeutral = calibrated->GetRigRuntimeContext()->RigLogic->GetNeutralJointValues();
		if (!TestEqual(TEXT("The calibrated DNA preserves neutral channel count"), newNeutral.Num(), oldNeutral.Num()))
		{
			return false;
		}

		const int32 channels = newNeutral.Num() / count;
		for (uint16 joint = 0; joint < count; joint++)
		{
			const int32 offset = joint * channels;
			if (!TestTrue(TEXT("RigLogic uses the rebased neutral local translation"), FVector(newNeutral[offset], newNeutral[offset + 1], newNeutral[offset + 2]).Equals(FVector(translations[joint].x, translations[joint].y, translations[joint].z), 0.0001)))
			{
				return false;
			}

			for (int32 channel = 3; channel < channels; channel++)
			{
				if (!TestEqual(TEXT("Calibration preserves neutral rotation and scale"), newNeutral[offset + channel], oldNeutral[offset + channel]))
				{
					return false;
				}
			}
		}

		if (!TestEqual(TEXT("Calibration preserves RBF solver count"), calibratedReader->GetRBFSolverCount(), reader->GetRBFSolverCount()) || !TestEqual(TEXT("Calibration preserves twist solver count"), calibratedReader->GetTwistCount(), reader->GetTwistCount()) || !TestEqual(TEXT("Calibration preserves swing solver count"), calibratedReader->GetSwingCount(), reader->GetSwingCount()) || !TestEqual(TEXT("Calibration preserves joint group count"), calibratedReader->GetJointGroupCount(), reader->GetJointGroupCount()))
		{
			return false;
		}
		for (uint16 group = 0; group < reader->GetJointGroupCount(); group++)
		{
			const TArrayView<const float> original = reader->GetJointGroupValues(group);
			const TArrayView<const float> copied = calibratedReader->GetJointGroupValues(group);
			if (!TestTrue(TEXT("Calibration preserves joint behavior coefficients"), original.Num() == copied.Num() && FMemory::Memcmp(original.GetData(), copied.GetData(), original.Num() * sizeof(float)) == 0))
			{
				return false;
			}
		}

		FSavePackageArgs arguments;
		arguments.TopLevelFlags = RF_Public | RF_Standalone;
		if (!TestTrue(TEXT("The private dwarf DNA saves"), UPackage::SavePackage(package, calibrated, *FPackageName::LongPackageNameToFilename(packageName, FPackageName::GetAssetPackageExtension()), arguments)))
		{
			return false;
		}

		UDNAAssetUserData* privateData = NewObject<UDNAAssetUserData>(mesh);
		privateData->DNAAsset = calibrated;
		mesh->AddAssetUserData(privateData);
		if (!TestTrue(TEXT("The dwarf owns its private rig binding"), mesh->GetAssetUserData<UDNAAssetUserData>() == privateData && privateData->GetOuter() == mesh))
		{
			return false;
		}

		mesh->Modify();
		TestTrue(TEXT("The dwarf body saves its private rig binding"), UPackage::SavePackage(mesh->GetOutermost(), mesh, *FPackageName::LongPackageNameToFilename(mesh->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), arguments));
		AddInfo(FString::Printf(TEXT("CALIBRATED %s: %d joints, %d channels per joint, source %s, private %s."), *donor, count, channels, *source->GetPathName(), *calibrated->GetPathName()));
		TestEqual(TEXT("Rig binding preserves all approved geometry, reference bones, frames and weights"), dwarfTestGeometryFingerprint(*body), originalGeometry);
		body->InitAnim(true);
	}
#endif

	{
		const bool previousDisabled = body->GetDisablePostProcessBlueprint();
		const USkeletalMesh* mesh = body->GetSkeletalMeshAsset();
		const FReferenceSkeleton& reference = mesh->GetRefSkeleton();
		AddInfo(FString::Printf(TEXT("Joint rig %s: %s."), *donor, *GetNameSafe(body->GetPostProcessInstance())));
		body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		for (const TCHAR* name : { TEXT("Reference"), TEXT("Walk_Loop"), TEXT("Jog_Fwd_Loop") })
		{
			UAnimationAsset* asset = FCString::Strcmp(name, TEXT("Reference")) == 0 ? nullptr : LoadObject<UAnimationAsset>(nullptr, *FString::Printf(TEXT("/Game/Characters/Animations/UAL/%s.%s"), name, name));
			body->SetAnimation(asset);
			UAnimSingleNodeInstance* player = body->GetSingleNodeInstance();
			const float length = player->GetLength();
			for (int32 sample = 0; sample < (asset == nullptr ? 1 : 4); sample++)
			{
				TArray<FVector3f> surfaces[2];
				TArray<FTransform> locals[2];
				for (int32 pass = 0; pass < 2; pass++)
				{
					body->SetDisablePostProcessBlueprint(pass == 0);
					body->SetPosition(length * sample / 4.0f, false);
					body->TickAnimation(0.0f, false);
					body->RefreshBoneTransforms();
					locals[pass] = body->GetBoneSpaceTransforms();
					TArray<FMatrix44f> matrices;
					body->GetCurrentRefToLocalMatrices(matrices, 0);
					USkinnedMeshComponent::ComputeSkinnedPositions(body, surfaces[pass], matrices, body->GetSkeletalMeshRenderData()->LODRenderData[0], *body->GetSkinWeightBuffer(0));
				}

				double maximum = 0.0;
				double translation = 0.0;
				double angle = 0.0;
				FName moved;
				FName turned;
				for (int32 vertex = 0; vertex < surfaces[0].Num(); vertex++)
				{
					maximum = FMath::Max(maximum, static_cast<double>(FVector3f::Distance(surfaces[0][vertex], surfaces[1][vertex])));
				}

				for (int32 bone = 0; bone < locals[0].Num(); bone++)
				{
					const double offset = FVector::Distance(locals[0][bone].GetTranslation(), locals[1][bone].GetTranslation());
					const double rotation = FMath::RadiansToDegrees(locals[0][bone].GetRotation().AngularDistance(locals[1][bone].GetRotation()));
					if (offset > translation)
					{
						translation = offset;
						moved = reference.GetBoneName(bone);
					}

					if (rotation > angle)
					{
						angle = rotation;
						turned = reference.GetBoneName(bone);
					}
				}

				AddInfo(FString::Printf(TEXT("JOINT_AB %s %s phase%d: skin %.6f cm, local translation %.6f cm at %s, rotation %.6f degrees at %s."), *donor, name, sample, maximum, translation, *moved.ToString(), angle, *turned.ToString()));
				if (asset == nullptr)
				{
					TestTrue(TEXT("Donor correctives preserve the approved neutral dwarf surface"), maximum < 0.01);
				}
			}
		}

		body->SetDisablePostProcessBlueprint(previousDisabled);
	}

	TestEqual(TEXT("The body uses the organized dwarf package"), body->GetSkeletalMeshAsset()->GetPathName(), FString::Printf(TEXT("/Game/Characters/MetaHumans/Dwarfs/%s/%s/Body/SKM_Dwarf_%s_Body.SKM_Dwarf_%s_Body"), sex, *donor, *donor, *donor));
	TestEqual(TEXT("The face uses the organized dwarf package"), face->GetSkeletalMeshAsset()->GetPathName(), FString::Printf(TEXT("/Game/Characters/MetaHumans/Dwarfs/%s/%s/Face/SKM_MH_%s_FaceMesh_Preview.SKM_MH_%s_FaceMesh_Preview"), sex, *donor, *donor, *donor));
	TestTrue(TEXT("The dwarf stands in the roster's proportions"), dwarf->GetActorScale3D().Equals(FVector(1.12, 1.08, 0.90), 0.001));
	TestTrue(TEXT("The host keeps its gameplay scale"), proxy->GetActorScale3D().Equals(FVector::OneVector, 0.001));
	const UDNAAssetUserData* rigData = body->GetSkeletalMeshAsset()->GetAssetUserData<UDNAAssetUserData>();
	if (!TestNotNull(TEXT("The dwarf body retains DNA userdata"), rigData))
	{
		return false;
	}

	const UDNA* bodyDna = rigData->DNAAsset.Get();
	if (!TestNotNull(TEXT("The dwarf retains calibrated body DNA"), bodyDna))
	{
		return false;
	}

	TestEqual(TEXT("The private DNA uses the organized dwarf package"), bodyDna->GetPathName(), FString::Printf(TEXT("/Game/Characters/MetaHumans/Dwarfs/%s/%s/Body/DNA_Dwarf_%s_Body.DNA_Dwarf_%s_Body"), sex, *donor, *donor, *donor));
	TestFalse(TEXT("The dwarf joint corrective rig is enabled"), body->GetDisablePostProcessBlueprint());
	body->SetDisablePostProcessBlueprint(true);
	Appearance::apply(*dwarf, look);
	TestFalse(TEXT("Refreshing the dwarf restores its corrective rig"), body->GetDisablePostProcessBlueprint());
	TestEqual(TEXT("A dwarf stands three quarters of a human"), Appearance::heightScale(*dwarf), 0.75f);
	TestTrue(TEXT("A dwarf body has the dwarf's shape"), Appearance::sameShape(*dwarf, look));
	Network::FAppearance human = look;
	human.species = Network::EAppearanceSpecies::Human;
	TestFalse(TEXT("A dwarf body is not the shape of its human donor"), Appearance::sameShape(*dwarf, human));
	const UGroomComponent* hair = FindObjectFast<UGroomComponent>(dwarf, TEXT("Hair"));
	TestTrue(TEXT("The dwarf wears its donor's hair"), (hair != nullptr) && (hair->GroomAsset != nullptr) && (hair->BindingAsset != nullptr));

	// The same donor as a human, to measure the dwarf against.
	AActor* donorBody = dwarfTestBodyOf(*world, *dwarfTestProxy(*world, human, FVector(1000.0, 200.0, 300.0)));
	USkeletalMeshComponent* donorMesh = (donorBody != nullptr) ? FindObjectFast<USkeletalMeshComponent>(donorBody, TEXT("Body")) : nullptr;
	if (!TestNotNull(TEXT("The human donor stands too"), donorMesh))
	{
		return false;
	}

	TestTrue(TEXT("The human donor is not dwarf shaped"), Appearance::sameShape(*donorBody, human) && (Appearance::heightScale(*donorBody) == 1.0f));
	for (const Appearance::EMotion motion : { Appearance::EMotion::Idle, Appearance::EMotion::Run, Appearance::EMotion::Walk })
	{
		const bool running = (motion != Appearance::EMotion::Idle);
		for (AActor* standing : { dwarf, donorBody })
		{
			Appearance::animate(*standing, motion, running ? 300.0f : 0.0f);
		}

		UAnimSingleNodeInstance* clip = body->GetSingleNodeInstance();
		if (!TestNotNull(TEXT("The dwarf plays a clip"), clip))
		{
			return false;
		}

		const int32 samples = 12;
		double lowest = MAX_dbl;
		double highest = -MAX_dbl;
		double neckGap = 0.0;
		double dwarfHead = 0.0;
		double donorHead = 0.0;
		double donorLowest = MAX_dbl;
		for (int32 frame = 0; frame <= samples; frame++)
		{
			const float time = clip->GetLength() * frame / samples;

			// The face copies the body's pose, so the body is posed first.
			for (USkeletalMeshComponent* mesh : { body, donorMesh })
			{
				mesh->SetPosition(time, false);
				mesh->TickAnimation(0.0f, false);
				mesh->RefreshBoneTransforms();
			}

			const FReferenceSkeleton& reference = body->GetSkeletalMeshAsset()->GetRefSkeleton();
			for (const FName bone : { FName(TEXT("lowerarm_l")), FName(TEXT("hand_l")), FName(TEXT("middle_03_l")), FName(TEXT("calf_l")), FName(TEXT("foot_l")) })
			{
				const int32 index = reference.FindBoneIndex(bone);
				if (index != INDEX_NONE)
				{
					const double expectedLength = reference.GetRefBonePose()[index].GetTranslation().Length();
					const double actualLength = body->GetBoneSpaceTransforms()[index].GetTranslation().Length();
					TestEqual(*FString::Printf(TEXT("%s retains the approved %s length"), *donor, *bone.ToString()), actualLength, expectedLength, 0.05);
				}
			}

			if (frame == 0)
			{
				double greatestOffset = 0.0;
				FName greatestBone;
				for (const FSkelMeshRenderSection& section : body->GetSkeletalMeshRenderData()->LODRenderData[0].RenderSections)
				{
					for (const FBoneIndexType index : section.BoneMap)
					{
						const FName bone = reference.GetBoneName(index);
						if ((bone == TEXT("root")) || (bone == TEXT("pelvis")))
						{
							continue;
						}
						const double offset = FVector::Distance(body->GetBoneSpaceTransforms()[index].GetTranslation(), reference.GetRefBonePose()[index].GetTranslation());
						if (offset > greatestOffset)
						{
							greatestOffset = offset;
							greatestBone = bone;
						}
					}
				}
				AddInfo(FString::Printf(TEXT("%s motion %d skinned reference translation offset %.6f cm at %s."), *donor, static_cast<int32>(motion), greatestOffset, *greatestBone.ToString()));
			}

			face->TickAnimation(0.0f, false);
			face->RefreshBoneTransforms();
			double sole = 0.0;
			double head = 0.0;
			dwarfTestMeasure(*body, *dwarf, sole, head);
			lowest = FMath::Min(lowest, sole);
			highest = FMath::Max(highest, sole);
			dwarfHead += head / (samples + 1);
			for (const TCHAR* bone : { TEXT("neck_01"), TEXT("head") })
			{
				neckGap = FMath::Max(neckGap, FVector::Distance(body->GetSocketLocation(bone), face->GetSocketLocation(bone)));
			}

			dwarfTestMeasure(*donorMesh, *donorBody, sole, head);
			donorLowest = FMath::Min(donorLowest, sole);
			donorHead += head / (samples + 1);
		}

		const double ratio = dwarfHead / donorHead;
		AddInfo(FString::Printf(TEXT("%s %s: sole %.2f to %.2f cm (donor lowest %.2f), head at %.0f of the donor's %.0f cm (%.3f), neck gap %.3f cm."), *donor, (motion == Appearance::EMotion::Idle) ? TEXT("standing") : (motion == Appearance::EMotion::Walk) ? TEXT("walking") : TEXT("running"), lowest, highest, donorLowest, dwarfHead, donorHead, ratio, neckGap));
		TestTrue(TEXT("The face follows the dwarf's neck"), neckGap < 0.1);
		TestTrue(TEXT("The dwarf's feet meet the ground as its donor's do"), FMath::Abs(lowest - donorLowest) < 1.5);
		TestTrue(TEXT("The dwarf stands about three quarters of its donor"), FMath::Abs(ratio - 0.75) < 0.04);
		// A jog leaves the ground between strides, where a walk always has a foot on it.
		if (motion == Appearance::EMotion::Run)
		{
			TestTrue(TEXT("The dwarf's jog leaves the ground"), (highest - lowest) > 4.0);
		}
	}

	// This client's own character, whose camera stands at the dwarf's eyes.
	AWorldCharacter* player = world->SpawnActor<AWorldCharacter>();
	player->SetActorLocation(FVector(100.0, 2000.0, 300.0));
	player->wear(look);
	if (!TestNotNull(TEXT("The local dwarf stands a body"), dwarfTestBodyOf(*world, *player)))
	{
		return false;
	}

	const USpringArmComponent* arm = player->FindComponentByClass<USpringArmComponent>();
	if (TestNotNull(TEXT("The local dwarf has a camera arm"), arm))
	{
		TestEqual(TEXT("The third-person camera follows the shorter body"), arm->GetRelativeLocation().Z, 150.0 * 0.75 - 90.0, 0.01);
	}

	return true;
}

#endif
