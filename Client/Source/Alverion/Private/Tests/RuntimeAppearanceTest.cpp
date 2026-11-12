#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSingleNodeInstance.h"
#include "Appearance.h"
#include "Appearance/AppearanceApplier.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Chaos/Box.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "Chaos/Capsule.h"
#include "Chaos/Sphere.h"
#include "ChaosCloth/ChaosClothingSimulation.h"
#include "ChaosCloth/ChaosClothingSimulationCloth.h"
#include "ChaosCloth/ChaosClothingSimulationCollider.h"
#include "ChaosCloth/ChaosClothingSimulationFactory.h"
#include "ChaosCloth/ChaosClothingSimulationSolver.h"
#include "ChaosClothAsset/ClothAssetBase.h"
#include "ChaosClothAsset/ClothAssetSKMClothingAsset.h"
#include "ChaosClothAsset/ClothAssetSKMClothingSimulation.h"
#include "ChaosClothAsset/ClothSimulationModel.h"
#include "ClothingAsset.h"
#include "ClothingSimulationInstance.h"
#include "ClothingSimulationInteractor.h"
#include "ClothingSystemRuntimeTypes.h"
#include "Dom/JsonObject.h"
#include "Components/PointLightComponent.h"
#include "Components/LODSyncComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GroomComponent.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Serialization/JsonSerializer.h"
#include "Utils/ClothingMeshUtils.h"
#include "UnrealClient.h"
#include "World/ProxyCharacter.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRuntimeAppearanceTest, "Alverion.Characters.RuntimeAppearance", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
	const TCHAR* const runtimeDonors[2][4] = { { TEXT("Jelena"), TEXT("Vivian"), TEXT("Aera"), TEXT("Ada") }, { TEXT("Victor"), TEXT("Orlando"), TEXT("Kelvin"), TEXT("Cameron") } };
	const TCHAR* const runtimePieces[] = { TEXT("Hat"), TEXT("Shirt"), TEXT("Pants"), TEXT("Gloves"), TEXT("Boots"), TEXT("BreastWrap"), TEXT("Braies") };

	/** Exercises ordinary character loading and movement in the active standalone game viewport. */
	class FRuntimeAppearanceCommand : public IAutomationLatentCommand
	{
	public:
		FRuntimeAppearanceCommand(FAutomationTestBase& test, const FString& parameters) : _test(test), _parameters(parameters)
		{
			_look.species = Network::EAppearanceSpecies::Dwarf;
			if (parameters.StartsWith(TEXT("Human")))
			{
				_look.species = Network::EAppearanceSpecies::Human;
			}
			else if (parameters.StartsWith(TEXT("Elf")))
			{
				_look.species = Network::EAppearanceSpecies::Elf;
			}
			else if (parameters.StartsWith(TEXT("Orc")))
			{
				_look.species = Network::EAppearanceSpecies::Orc;
			}

			_wardrobePreview = FParse::Param(FCommandLine::Get(), TEXT("WardrobePreview"));
			_captureSide = FParse::Param(FCommandLine::Get(), TEXT("WardrobeLeftSide")) ? -1 : 1;
			_rearCapture = FParse::Param(FCommandLine::Get(), TEXT("WardrobeRearView"));
			_fullOrbit = _wardrobePreview && FParse::Param(FCommandLine::Get(), TEXT("WardrobeFullOrbit"));
			if (_wardrobePreview)
			{
				_clothVerify = FParse::Param(FCommandLine::Get(), TEXT("WardrobeClothVerify"));
			}
			_race = _look.species == Network::EAppearanceSpecies::Human ? TEXT("Human") : _look.species == Network::EAppearanceSpecies::Elf ? TEXT("Elf") : _look.species == Network::EAppearanceSpecies::Orc ? TEXT("Orc") : TEXT("Dwarf");
			_look.male = parameters.Contains(TEXT("Male")) && !parameters.Contains(TEXT("Female"));
			_look.head = static_cast<uint8>(parameters[parameters.Len() - 1] - TEXT('A'));
			_look.eyeColor = 255;
			_look.hairStyle = (_look.species == Network::EAppearanceSpecies::Orc) ? (_look.male ? 27 : 16) : (_look.male ? 6 : 14);
			_look.hairColor = (_look.species == Network::EAppearanceSpecies::Orc) ? (_look.male ? 2 : 1) : (_wardrobePreview ? 1 : 0);
			_look.eyebrows = _look.male ? 12 : 10;
			_look.beard = (_look.species == Network::EAppearanceSpecies::Dwarf && _look.male) ? 9 : 0;
			_look.mustache = (_look.species == Network::EAppearanceSpecies::Dwarf && _look.male) ? 12 : 0;
			_donor = runtimeDonors[_look.male ? 1 : 0][_look.head];
			FParse::Value(FCommandLine::Get(), TEXT("WardrobePiece="), _pieceOnly);
			if (!_pieceOnly.IsEmpty())
			{
				_validPiece = false;
				for (const TCHAR* piece : runtimePieces)
				{
					if (_pieceOnly == piece)
					{
						_validPiece = true;
						break;
					}
				}

				if (_look.male && _pieceOnly == TEXT("BreastWrap"))
				{
					_validPiece = false;
				}
			}

			_captureHeight = _look.species == Network::EAppearanceSpecies::Dwarf ? 70.0 : 99.0;
			if (_look.species == Network::EAppearanceSpecies::Human)
			{
				if (FParse::Param(FCommandLine::Get(), TEXT("WardrobeTorsoCapture")))
				{
					_captureHeight = 145.0;
					_captureDistance = 370.0;
				}

				_handCapture = FParse::Param(FCommandLine::Get(), TEXT("WardrobeHandCapture"));
				if (_handCapture)
				{
					_captureDistance = 115.0;
				}
			}
			_customCamera = FParse::Value(FCommandLine::Get(), TEXT("AppearanceCaptureHeight="), _captureHeight);
			_customCamera |= FParse::Value(FCommandLine::Get(), TEXT("AppearanceCaptureDistance="), _captureDistance);
			_output = FPaths::ProjectSavedDir() / TEXT("RuntimeAppearance");
			FParse::Value(FCommandLine::Get(), TEXT("AppearanceCaptureDir="), _output);
			IFileManager::Get().MakeDirectory(*_output, true);
		}

		virtual bool Update() override
		{
			if (!_validPiece)
			{
				_test.AddError(TEXT("Unknown or unavailable wardrobe piece for ") + _donor + TEXT(": ") + _pieceOnly);
				return true;
			}

			const double now = FPlatformTime::Seconds();
			if ((_started > 0.0) && ((now - _started) > 180.0))
			{
				_test.AddError(_parameters + TEXT(" timed out while loading or capturing."));
				cleanup();
				return true;
			}

			if (_proxy.IsValid() && _camera.IsValid())
			{
				const FVector origin = _proxy->GetActorLocation();
				const FRotator orbit(0.0, static_cast<double>(_angle * _captureSide + (_rearCapture ? 180 : 0)), 0.0);
				const double height = _captureHeight;
				FVector centre = origin + FVector(0.0, 0.0, height);
				FVector position = origin + orbit.RotateVector(FVector(_captureDistance, 0.0, height + 3.0));
				if (_handCapture)
				{
					if (_body.IsValid())
					{
						centre = (_body->GetBoneLocation(TEXT("hand_l")) + _body->GetBoneLocation(TEXT("middle_03_l"))) * 0.5;
						position = centre + orbit.RotateVector(FVector(_captureDistance, 0.0, 3.0));
					}
				}
				_camera->SetActorLocationAndRotation(position, (centre - position).Rotation());
				for (int32 index = 0; index < _lights.Num(); index++)
				{
					_lights[index]->SetActorLocation(origin + orbit.RotateVector(_lightOffsets[index]));
				}
				_wall->SetActorLocationAndRotation(origin + orbit.RotateVector(FVector(-270.0, 0.0, 110.0)), orbit);
			}

			switch (_phase)
			{
				case 0:
				{
					if (_started == 0.0)
					{
						_started = now;
					}
					for (const FWorldContext& context : GEngine->GetWorldContexts())
					{
						if (context.WorldType == EWorldType::Game)
						{
							_world = context.World();
							break;
						}
					}
					if (!_world.IsValid() || (GEngine->GameViewport == nullptr))
					{
						_test.AddError(TEXT("Run this test in a standalone -game viewport, not an editor preview or commandlet."));
						return true;
					}
					_controller = _world->GetFirstPlayerController();
					if (!_controller.IsValid())
					{
						return false;
					}
					_oldView = _controller->GetViewTarget();
					UStaticMesh* cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
					UMaterialInterface* material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
					AStaticMeshActor* floor = _world->SpawnActor<AStaticMeshActor>();
					_owned.Add(floor);
					floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
					floor->GetStaticMeshComponent()->SetStaticMesh(cube);
					floor->SetActorLocation(FVector(0.0, 0.0, 990.0));
					floor->SetActorScale3D(FVector(200.0, 200.0, 0.2));
					floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
					floor->GetStaticMeshComponent()->CreateDynamicMaterialInstance(0, material)->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.29f, 0.285f, 0.28f, 1.0f));
					_wall = _world->SpawnActor<AStaticMeshActor>();
					_owned.Add(_wall.Get());
					_wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
					_wall->GetStaticMeshComponent()->SetStaticMesh(cube);
					_wall->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					_wall->SetActorScale3D(FVector(0.1, 11.0, 9.0));
					_wall->GetStaticMeshComponent()->CreateDynamicMaterialInstance(0, material)->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.36f, 0.345f, 0.35f, 1.0f));
					_lightOffsets = { FVector(83.0, 117.0, 205.0), FVector(122.0, -78.0, 136.0), FVector(-95.0, -43.0, 176.0), FVector(82.0, 60.0, 54.0) };
					const float powers[] = { 7.2f, 1.65f, 3.2f, 3.25f };
					for (int32 index = 0; index < _lightOffsets.Num(); index++)
					{
						APointLight* light = _world->SpawnActor<APointLight>();
						_owned.Add(light);
						_lights.Add(light);
						light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
						UPointLightComponent* point = CastChecked<UPointLightComponent>(light->GetLightComponent());
						point->SetIntensityUnits(ELightUnits::EV);
						point->SetIntensity(powers[index]);
						point->SetSourceRadius(index == 0 ? 52.0f : index == 1 ? 60.0f : index == 2 ? 53.0f : 62.0f);
						point->SetSpecularScale(index == 0 ? 0.7f : index == 2 ? 0.25f : 0.52f);
						point->SetCastShadows(false);
					}
					_camera = _world->SpawnActor<ACameraActor>();
					_owned.Add(_camera.Get());
					UCameraComponent* camera = _camera->GetCameraComponent();
					camera->FieldOfView = _look.species == Network::EAppearanceSpecies::Dwarf ? 11.7f : 14.85f;
					camera->AspectRatio = 0.87f;
					camera->bConstrainAspectRatio = true;
					camera->PostProcessSettings.bOverride_AutoExposureBias = true;
					camera->PostProcessSettings.AutoExposureBias = -5.2f;
					camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
					camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
					camera->PostProcessSettings.AutoExposureMinBrightness = 0.25f;
					camera->PostProcessSettings.AutoExposureMaxBrightness = 0.25f;
					camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
					camera->PostProcessSettings.MotionBlurAmount = 0.0f;
					_controller->SetViewTarget(_camera.Get());
					_proxy = _world->SpawnActor<AProxyCharacter>();
					_owned.Add(_proxy.Get());
					Network::FCharacterSpawn spawn;
					spawn.location = FVector(0.0, 0.0, 1000.0);
					spawn.appearance = _look.pack();
					spawn.runSpeed = 300.0f;
					spawn.walkSpeed = 110.0f;
					spawn.running = false;
					_proxy->setup(spawn);
					const FVector cameraLocation = spawn.location + FVector(_captureDistance, 0.0, _captureHeight + 3.0);
					_camera->SetActorLocationAndRotation(cameraLocation, (spawn.location + FVector(0.0, 0.0, _captureHeight) - cameraLocation).Rotation());
					_phase = 1;
					break;
				}
				case 1:
				{
					TArray<AActor*> attached;
					_proxy->GetAttachedActors(attached);
					if (attached.Num() != 1)
					{
						break;
					}
					_visual = attached[0];
					_body = FindObjectFast<USkeletalMeshComponent>(_visual.Get(), TEXT("Body"));
					_face = FindObjectFast<USkeletalMeshComponent>(_visual.Get(), TEXT("Face"));
					if (!_body.IsValid() || !_face.IsValid())
					{
						break;
					}
					const bool orc = _look.species == Network::EAppearanceSpecies::Orc;
					const bool dwarf = _look.species == Network::EAppearanceSpecies::Dwarf;
					const bool human = _look.species == Network::EAppearanceSpecies::Human;
					const TCHAR* hairName = orc ? (_look.male ? TEXT("Hair_S_PulledBack") : TEXT("Hair_S_Cornrows")) : (_look.male ? TEXT("Hair_L_MessyClumps") : TEXT("Hair_S_UpdoBraids"));
					const TCHAR* browName = _look.male ? TEXT("Eyebrows_M_Unruly") : TEXT("Eyebrows_M_Swept");
					const TCHAR* slots[] = { TEXT("Hair"), TEXT("Eyebrows"), TEXT("Beard"), TEXT("Mustache") };
					const TCHAR* assets[] = { hairName, browName, TEXT("Beard_L_Full"), TEXT("Mustache_L_Handlebar") };
					bool ready = true;
					for (int32 index = 0; index < (dwarf && _look.male ? 4 : 2); index++)
					{
						const UGroomComponent* groom = FindObjectFast<UGroomComponent>(_visual.Get(), slots[index]);
						if ((groom == nullptr) || (groom->GroomAsset == nullptr) || (groom->BindingAsset == nullptr))
						{
							ready = false;
							break;
						}
						if ((groom->GroomAsset->GetName() != assets[index]) || (groom->BindingAsset->GetGroom() != groom->GroomAsset))
						{
							ready = false;
							break;
						}
					}
					if (!ready)
					{
						break;
					}

					_test.TestTrue(TEXT("The game uses the approved actor proportions"), _visual->GetActorScale3D().Equals(dwarf ? FVector(1.12, 1.08, 0.9) : orc ? FVector(1.08) : FVector(1.0), 0.001));
					if (_wardrobePreview)
					{
						const FString donorFolder = FString(_look.male ? TEXT("Male/") : TEXT("Female/")) + _donor;
						FString bodyPath;
						if (human)
						{
							const FString bodyName = TEXT("SKM_MH_") + _donor + TEXT("_BodyMesh");
							bodyPath = TEXT("/Game/Characters/MetaHumans/Humans/") + donorFolder + TEXT("/Body/") + bodyName + TEXT(".") + bodyName;
						}
						else if (dwarf)
						{
							const FString bodyName = TEXT("SKM_Dwarf_") + _donor + TEXT("_Body");
							bodyPath = TEXT("/Game/Characters/MetaHumans/Dwarfs/") + donorFolder + TEXT("/Body/") + bodyName + TEXT(".") + bodyName;
						}
						else
						{
							const FString suffix = _look.head == 0 ? FString() : FString::Chr(TEXT('A') + _look.head);
							const FString name = TEXT("MH_") + _race + (_look.male ? TEXT("Male") : TEXT("Female")) + suffix;
							const FString bodyName = TEXT("SKM_") + name + TEXT("_BodyMesh");
							bodyPath = FString(TEXT("/Game/Characters/MetaHumans/")) + (orc ? TEXT("Orcs/") : TEXT("Elves/")) + donorFolder + TEXT("/Body/") + bodyName + TEXT(".") + bodyName;
						}
						USkeletalMesh* donorBody = _body->GetSkeletalMeshAsset();
						if (!_test.TestNotNull(TEXT("The original race body loads"), donorBody))
						{
							cleanup();
							return true;
						}

						_test.TestEqual(TEXT("The preview uses the exact original race body"), donorBody->GetPathName(), bodyPath);
						_donorBody = donorBody;
						_donorReference = donorBody->GetRefSkeleton().GetRefBonePose();
						const bool rags = true;
						const bool fallback = FParse::Param(FCommandLine::Get(), TEXT("WardrobeFallback"));
						const bool pieceFiltered = !_pieceOnly.IsEmpty();
						FString bodyFitDonor = _look.male ? TEXT("Victor") : TEXT("Jelena");
						if (dwarf)
						{
							bodyFitDonor = _donor;
						}
						else if (orc)
						{
							if (_look.male && _look.head != 0)
							{
								bodyFitDonor = TEXT("Orlando");
							}
						}

						TArray<USkeletalMeshComponent*> productionComponents;
						_visual->GetComponents<USkeletalMeshComponent>(productionComponents);
						if (!_test.TestTrue(TEXT("The shared starter outfit applies without creating duplicate components"), Appearance::dressStarter(*_visual.Get(), _look, _pieceOnly, fallback)))
						{
							cleanup();
							return true;
						}

						TArray<USkeletalMeshComponent*> repeatedComponents;
						_visual->GetComponents<USkeletalMeshComponent>(repeatedComponents);
						_test.TestEqual(TEXT("Repeated dressing preserves the production component count"), repeatedComponents.Num(), productionComponents.Num());
						for (int32 index = 0; index < UE_ARRAY_COUNT(runtimePieces); index++)
						{
							if (pieceFiltered && _pieceOnly != runtimePieces[index])
							{
								continue;
							}

							if (_look.male && index == 5)
							{
								continue;
							}

							const FString pieceName = FString(TEXT("SKM_Villager_")) + runtimePieces[index];
							const FString fitDonor = index == 0 ? _donor : bodyFitDonor;
							const FString meshName = index == 0 ? TEXT("SKM_Villager_Hat_Cowboy") : index == 5 ? TEXT("SKM_Villager_CroppedTop") : index == 6 ? TEXT("SKM_Villager_Pants") : pieceName;
							const FString piecePath = TEXT("/Game/Equipment/Clothing/Villager/Rags/") + _race + TEXT("/") + fitDonor + TEXT("/") + meshName + TEXT(".") + meshName;
							USkeletalMesh* piece = LoadObject<USkeletalMesh>(nullptr, *piecePath);
							if (!_test.TestNotNull(FString(TEXT("The garment loads: ")) + piecePath, piece))
							{
								cleanup();
								return true;
							}

							USkeletalMeshComponent* garment = FindObjectFast<USkeletalMeshComponent>(_visual.Get(), FName(*pieceName));
							if (!_test.TestNotNull(TEXT("Production creates the separate equipment component"), garment))
							{
								cleanup();
								return true;
							}

							_test.TestTrue(TEXT("The component already belongs to the production appearance path"), productionComponents.Contains(garment));
							_test.TestTrue(TEXT("Production loads the exact accepted equipment mesh"), garment->GetSkeletalMeshAsset() == piece);
							_test.TestEqual(TEXT("Production selects the requested armor or fallback visibility"), garment->IsVisible(), pieceFiltered || (fallback ? (index == 6 || (index == 5 && !_look.male)) : index < 5));
							garment->bWaitForParallelClothTask = _clothVerify;
							_garments.Add(garment);
							const FReferenceSkeleton& pieceReference = piece->GetRefSkeleton();
							const FReferenceSkeleton& bodyReference = donorBody->GetRefSkeleton();
							const int32 pieceBoneCount = pieceReference.GetNum();
							for (int32 boneIndex = 0; boneIndex < pieceBoneCount; boneIndex++)
							{
								const FName boneName = pieceReference.GetBoneName(boneIndex);
								const int32 bodyBoneIndex = bodyReference.FindBoneIndex(boneName);
								if (!_test.TestTrue(TEXT("The donor contains the garment bone"), bodyBoneIndex != INDEX_NONE))
								{
									continue;
								}

								_test.TestTrue(TEXT("The garment reference bone aligns with the donor"), pieceReference.GetRefBonePose()[boneIndex].Equals(bodyReference.GetRefBonePose()[bodyBoneIndex], 0.001));
								const int32 pieceParent = pieceReference.GetParentIndex(boneIndex);
								const int32 bodyParent = bodyReference.GetParentIndex(bodyBoneIndex);
								_test.TestEqual(TEXT("The garment and donor agree on root bones"), pieceParent == INDEX_NONE, bodyParent == INDEX_NONE);
								if (pieceParent != INDEX_NONE && bodyParent != INDEX_NONE)
								{
									_test.TestEqual(TEXT("The garment reference parent matches the donor"), pieceReference.GetBoneName(pieceParent), bodyReference.GetBoneName(bodyParent));
								}
							}
						}

						bool bootsVisible = false;
						bool glovesVisible = false;
						bool pantsVisible = false;
						bool ragBodyCovered = false;
						for (const TWeakObjectPtr<USkeletalMeshComponent>& garment : _garments)
						{
							if (garment->IsVisible())
							{
								bootsVisible |= garment->GetName() == TEXT("SKM_Villager_Boots");
								glovesVisible |= garment->GetName() == TEXT("SKM_Villager_Gloves");
								pantsVisible |= garment->GetName() == TEXT("SKM_Villager_Pants");
								ragBodyCovered |= garment->GetName() == TEXT("SKM_Villager_Shirt") || garment->GetName() == TEXT("SKM_Villager_Pants") || garment->GetName() == TEXT("SKM_Villager_Braies");
							}
						}

						const FString profileName = TEXT("MI_Coverage_") + _race + TEXT("_") + bodyFitDonor;
						const FString profilePath = TEXT("/Game/Equipment/Shared/Coverage/") + profileName + TEXT(".") + profileName;
						UMaterialInterface* profile = LoadObject<UMaterialInterface>(nullptr, *profilePath);
						const int32 bodySlot = _body->GetMaterialIndex(TEXT("body_shader_shader"));
						if (!_test.TestNotNull(TEXT("The equipment body coverage profile loads"), profile) || !_test.TestTrue(TEXT("The body skin slot exists"), bodySlot != INDEX_NONE))
						{
							cleanup();
							return true;
						}

						UMaterialInstanceDynamic* coverage = Cast<UMaterialInstanceDynamic>(_body->GetMaterial(bodySlot));
						if (!_test.TestNotNull(TEXT("Production installs the body coverage material"), coverage))
						{
							cleanup();
							return true;
						}

						_test.TestTrue(TEXT("Production uses the exact race coverage profile"), coverage->Parent == profile);
						_test.TestEqual(TEXT("Covered feet follow boot visibility"), coverage->K2_GetScalarParameterValue(TEXT("FootCoverage")), bootsVisible && !rags ? 1.0f : 0.0f);
						_test.TestEqual(TEXT("Covered hands follow glove visibility"), coverage->K2_GetScalarParameterValue(TEXT("HandCoverage")), glovesVisible ? 1.0f : 0.0f);
						_test.TestEqual(TEXT("Covered legs follow pants visibility and restore for underlayers"), coverage->K2_GetScalarParameterValue(TEXT("PantsCoverage")), (rags ? ragBodyCovered : pantsVisible) ? 1.0f : 0.0f);

						if (pieceFiltered ? _pieceOnly == TEXT("Hat") : !fallback)
						{
							const UGroomComponent* hair = FindObjectFast<UGroomComponent>(_visual.Get(), TEXT("Hair"));
							if (hair != nullptr)
							{
								_test.TestTrue(TEXT("Production retains visible hair below headwear"), hair->IsVisible());
							}
						}

						UStaticMeshComponent* weapon = FindObjectFast<UStaticMeshComponent>(_visual.Get(), TEXT("StarterWoodenSword"));
						if (!_test.TestNotNull(TEXT("Production equips the starter sword"), weapon))
						{
							cleanup();
							return true;
						}

						_test.TestEqual(TEXT("The sword is visible with the full starter outfit"), weapon->IsVisible(), !pieceFiltered && !fallback);
						_test.TestTrue(TEXT("The sword follows the pelvis at the left hip"), weapon->GetAttachParent() == _body.Get() && weapon->GetAttachSocketName() == FName(TEXT("pelvis")));
						const USkeletalMesh* swordBody = _body->GetSkeletalMeshAsset();
						const FTransform carryReference = weapon->GetRelativeTransform() * FTransform(swordBody->GetComposedRefPoseMatrix(FName(TEXT("pelvis"))));
						const FVector leftHip = swordBody->GetComposedRefPoseMatrix(FName(TEXT("thigh_l"))).GetOrigin();
						const FVector rightHip = swordBody->GetComposedRefPoseMatrix(FName(TEXT("thigh_r"))).GetOrigin();
						const FVector left = (leftHip - rightHip).GetSafeNormal2D();
						const FVector forward = FVector::CrossProduct(FVector::UpVector, left);
						const FVector bladeAxis = carryReference.GetUnitAxis(EAxis::Z);
						_test.TestTrue(TEXT("The stowed blade points down and slightly backward"), bladeAxis.Z < -0.9 && FVector::DotProduct(bladeAxis, forward) < -0.2 && FMath::Abs(FVector::DotProduct(bladeAxis, left)) < 0.01);
						const FVector guardOffset = carryReference.GetTranslation() - leftHip;
						_test.TestTrue(TEXT("The stowed guard clears the left hip at belt height"), FMath::Abs(FVector::DotProduct(guardOffset, left) - 14.0) < 0.01 && FMath::Abs(FVector::DotProduct(guardOffset, forward) - 2.0) < 0.01 && FMath::Abs(guardOffset.Z - 12.0) < 0.01);

					}

					if (dwarf && FParse::Param(FCommandLine::Get(), TEXT("DwarfCorrectiveProbe")))
					{
						_body->SetDisablePostProcessBlueprint(false);
						_test.AddInfo(TEXT("DIAGNOSTIC: donor body correctives enabled for joint-deformation comparison."));
					}

					int32 captureLod = -1;
					if (FParse::Value(FCommandLine::Get(), TEXT("AppearanceCaptureLOD="), captureLod))
					{
						ULODSyncComponent* sync = _visual->FindComponentByClass<ULODSyncComponent>();
						if (sync != nullptr)
						{
							sync->SetComponentTickEnabled(false);
						}
						_body->SetForcedLOD(FMath::Clamp(captureLod, 0, 3) + 1);
					}

					if (_handCapture || _customCamera)
					{
						// The close camera culls the face, but this test still verifies its attachment.
						_face->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
					}

					FString diagnosticPantsPath;
					if (FParse::Value(FCommandLine::Get(), TEXT("WardrobePantsMesh="), diagnosticPantsPath))
					{
						USkeletalMeshComponent* pants = FindObjectFast<USkeletalMeshComponent>(_visual.Get(), TEXT("SKM_Villager_Pants"));
						USkeletalMesh* diagnosticPants = LoadObject<USkeletalMesh>(nullptr, *diagnosticPantsPath);
						if (!_test.TestNotNull(TEXT("The diagnostic Pants component exists"), pants) || !_test.TestNotNull(TEXT("The diagnostic Pants mesh loads"), diagnosticPants))
						{
							cleanup();
							return true;
						}

						pants->SetSkeletalMeshAsset(diagnosticPants);
						pants->SetLeaderPoseComponent(_body.Get());
						pants->UnbindClothFromLeaderPoseComponent();
					}

					FString diagnosticShirtPath;
					if (FParse::Value(FCommandLine::Get(), TEXT("WardrobeShirtMesh="), diagnosticShirtPath))
					{
						USkeletalMeshComponent* shirt = FindObjectFast<USkeletalMeshComponent>(_visual.Get(), TEXT("SKM_Villager_Shirt"));
						USkeletalMesh* diagnosticShirt = LoadObject<USkeletalMesh>(nullptr, *diagnosticShirtPath);
						if (!_test.TestNotNull(TEXT("The diagnostic Shirt component exists"), shirt) || !_test.TestNotNull(TEXT("The diagnostic Shirt mesh loads"), diagnosticShirt))
						{
							cleanup();
							return true;
						}

						shirt->SetSkeletalMeshAsset(diagnosticShirt);
						shirt->SetLeaderPoseComponent(_body.Get());
						shirt->UnbindClothFromLeaderPoseComponent();
					}

					if (!_test.TestTrue(TEXT("The capture camera moves with the proxy before camera updates"), _camera->AttachToActor(_proxy.Get(), FAttachmentTransformRules::KeepWorldTransform)))
					{
						cleanup();
						return true;
					}

					if (!_test.TestTrue(TEXT("The capture wall moves with the proxy"), _wall->AttachToActor(_proxy.Get(), FAttachmentTransformRules::KeepWorldTransform)))
					{
						cleanup();
						return true;
					}

					for (const TWeakObjectPtr<APointLight>& light : _lights)
					{
						if (!_test.TestTrue(TEXT("The capture light moves with the proxy"), light->AttachToActor(_proxy.Get(), FAttachmentTransformRules::KeepWorldTransform)))
						{
							cleanup();
							return true;
						}
					}

					_phase = 2;
					_then = now;
					break;
				}
				case 2:
				{
					_proxy->standAt(FVector(0.0, 0.0, 1000.0), 0.0f);
					_proxy->setRunning(_motion == 2);
					if (_motion != 0)
					{
						_proxy->walkTo(FVector(100000.0, 0.0, 1000.0));
					}
					_then = now;
					_lastSample = now + 0.3;
					_nextVideo = now + 0.5;
					_videoFrames = 0;
					_minSole = MAX_dbl;
					_maxSole = -MAX_dbl;
					_maxNeck = 0.0;
					_maxBone = 0.0;
					_phase = 3;
					break;
				}
				case 3:
				{
					if (FParse::Param(FCommandLine::Get(), TEXT("DwarfFixedPoseProbe")))
					{
						UAnimSingleNodeInstance* player = _body->GetSingleNodeInstance();
						player->SetPosition(player->GetLength() * 0.25f, false);
						player->SetPlaying(false);
					}

					if (now > _lastSample)
					{
						_lastSample = now;
						TArray<FMatrix44f> matrices;
						TArray<FVector3f> positions;
						const FSkeletalMeshRenderData* renderData = _body->GetSkeletalMeshRenderData();
						const int32 renderLod = FMath::Clamp(_body->GetPredictedLODLevel(), 0, renderData->LODRenderData.Num() - 1);
						_body->GetCurrentRefToLocalMatrices(matrices, renderLod);
						USkinnedMeshComponent::ComputeSkinnedPositions(_body.Get(), positions, matrices, renderData->LODRenderData[renderLod], *_body->GetSkinWeightBuffer(renderLod));
						double sole = MAX_dbl;
						for (const FVector3f& point : positions)
						{
							sole = FMath::Min(sole, _body->GetComponentTransform().TransformPosition(FVector(point)).Z - 1000.0);
						}
						_minSole = FMath::Min(_minSole, sole);
						_maxSole = FMath::Max(_maxSole, sole);
						_maxNeck = FMath::Max(_maxNeck, FVector::Distance(_body->GetSocketLocation(TEXT("head")), _face->GetSocketLocation(TEXT("head"))));
						const FReferenceSkeleton& reference = _body->GetSkeletalMeshAsset()->GetRefSkeleton();
						for (const FName bone : { FName(TEXT("lowerarm_l")), FName(TEXT("hand_l")), FName(TEXT("middle_03_l")), FName(TEXT("calf_l")), FName(TEXT("foot_l")) })
						{
							const int32 index = reference.FindBoneIndex(bone);
							if (index != INDEX_NONE)
							{
								_maxBone = FMath::Max(_maxBone, FMath::Abs(_body->GetBoneSpaceTransforms()[index].GetTranslation().Length() - reference.GetRefBonePose()[index].GetTranslation().Length()));
							}
						}
					}
					if ((_look.head == 0) && (_motion != 0) && (_angle == 0) && (_videoFrames < 12) && (now > _nextVideo) && !FScreenshotRequest::IsScreenshotRequested())
					{
						const FString frame = _output / FString::Printf(TEXT("%s_%s_%s_Sequence%02d.png"), *_parameters, *_donor, motionName(), _videoFrames++);
						if (!verifyWardrobeCloth(frame) || !dumpWardrobePose(frame))
						{
							cleanup();
							return true;
						}

						FScreenshotRequest::RequestScreenshot(frame, false, false, false, FIntRect(), true);
						_nextVideo = now + 0.15;
					}
					if (((now - _then) > 3.0) && !FScreenshotRequest::IsScreenshotRequested())
					{
						UAnimSingleNodeInstance* animation = _body->GetSingleNodeInstance();
						const UAnimationAsset* asset = animation != nullptr ? animation->GetAnimationAsset() : nullptr;
						const FString clip = asset != nullptr ? asset->GetName() : FString();
						const TCHAR* expected = _motion == 0 ? TEXT("Idle_Selection_Loop") : _motion == 1 ? TEXT("Walk_Loop") : TEXT("Jog_Fwd_Loop");
						_test.TestEqual(TEXT("The real movement selects the expected clip"), clip, FString(expected));
						if (_donorBody.IsValid())
						{
							_test.TestTrue(TEXT("Movement keeps the original donor body"), _body->GetSkeletalMeshAsset() == _donorBody.Get());
							const TArray<FTransform>& reference = _donorBody->GetRefSkeleton().GetRefBonePose();
							const int32 referenceCount = reference.Num();
							if (_test.TestEqual(TEXT("The donor reference bone count is unchanged"), referenceCount, _donorReference.Num()))
							{
								for (int32 index = 0; index < referenceCount; index++)
								{
									_test.TestTrue(TEXT("The donor reference pose is unchanged"), reference[index].Equals(_donorReference[index], 0.0));
								}
							}

							UStaticMeshComponent* weapon = FindObjectFast<UStaticMeshComponent>(_visual.Get(), TEXT("StarterWoodenSword"));
							if (_test.TestNotNull(TEXT("Movement retains the production sword"), weapon))
							{
								const FVector expectedWeaponScale(Appearance::heightScale(*_visual));
								FTransform expectedCarry = weapon->GetRelativeTransform() * _body->GetSocketTransform(TEXT("pelvis"));
								expectedCarry.SetScale3D(expectedWeaponScale);
								_test.TestTrue(TEXT("The sword retains its rigid proportions during movement"), weapon->GetComponentScale().Equals(expectedWeaponScale, 0.001));
								_test.TestTrue(TEXT("The moving sword stays attached to the left hip"), weapon->GetComponentTransform().Equals(expectedCarry, 0.01));
							}

							_test.TestEqual(TEXT("The preview keeps separate garment components for this donor"), _garments.Num(), _pieceOnly.IsEmpty() ? (_look.male ? 6 : 7) : 1);
							for (const TWeakObjectPtr<USkeletalMeshComponent>& garment : _garments)
							{
								if (_test.TestTrue(TEXT("The garment component remains valid"), garment.IsValid()))
								{
									_test.TestTrue(TEXT("The garment follows the donor pose"), garment->LeaderPoseComponent.Get() == _body.Get());
									_test.TestTrue(TEXT("The garment remains attached in donor space"), garment->GetAttachParent() == _body.Get() && garment->GetRelativeTransform().Equals(FTransform::Identity, 0.001));
									garment->RefreshBoneTransforms();
									for (const FName anchor : { FName(TEXT("head")), FName(TEXT("hand_l")), FName(TEXT("hand_r")), FName(TEXT("foot_l")), FName(TEXT("foot_r")) })
									{
										if (_test.TestTrue(TEXT("The garment contains the pose anchor"), garment->DoesSocketExist(anchor)))
										{
											_test.TestEqual(TEXT("The garment pose anchor follows the moving donor"), garment->GetSocketLocation(anchor), _body->GetSocketLocation(anchor), 0.01f);
										}
									}
								}
							}
						}

						_test.TestTrue(TEXT("The runtime face follows the body"), _maxNeck < 0.1);
						_test.TestTrue(TEXT("The runtime preserves approved limb and finger lengths"), _maxBone < 0.05);
						_test.TestTrue(TEXT("The stride meets the floor without sinking or floating"), FMath::Abs(_minSole) < 1.5);
						if (_motion == 1)
						{
							_test.TestTrue(TEXT("Walking retains a planted sole throughout each step"), FMath::Abs(_maxSole) < 1.5);
						}

						const TCHAR* angleLabel;
						switch (_angle)
						{
							case 0:
							{
								angleLabel = _rearCapture ? TEXT("Back") : TEXT("Front");
								break;
							}
							case 32:
							{
								angleLabel = _rearCapture ? TEXT("BackThreeQuarter") : TEXT("ThreeQuarter");
								break;
							}
							case 148:
							{
								angleLabel = _rearCapture ? TEXT("ThreeQuarter") : TEXT("BackThreeQuarter");
								break;
							}
							default:
							{
								angleLabel = TEXT("Profile");
								break;
							}
						}
						_screenshot = _output / FString::Printf(TEXT("%s_%s_%s_%s.png"), *_parameters, *_donor, motionName(), angleLabel);
						const FString record = FString::Printf(TEXT("%s,%s,%s,%d,%s,%.5f,%.5f,%.5f,%.5f,%.3f,%s\n"), *_parameters, *_donor, motionName(), _angle * _captureSide + (_rearCapture ? 180 : 0), *clip, _minSole, _maxSole, _maxNeck, _maxBone, _proxy->GetActorLocation().X, *_body->GetSkeletalMeshAsset()->GetPathName());
						FFileHelper::SaveStringToFile(record, *(_output / TEXT("runtime-poses.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
						if (IFileManager::Get().FileExists(*_screenshot))
						{
							_test.AddError(TEXT("Capture target already exists; use a fresh AppearanceCaptureDir."));
							cleanup();
							return true;
						}

						if (!verifyWardrobeCloth(_screenshot) || !dumpWardrobePose(_screenshot))
						{
							cleanup();
							return true;
						}

						_test.AddInfo(FString::Printf(TEXT("Runtime body detail level %s: %d."), *_parameters, _body->GetPredictedLODLevel()));
						FVector viewLocation;
						FRotator viewRotation;
						_controller->GetPlayerViewPoint(viewLocation, viewRotation);
						_test.TestTrue(TEXT("The rendered camera follows the moving capture target without a stale position"), viewLocation.Equals(_camera->GetActorLocation(), 0.01));
						_test.TestTrue(TEXT("The rendered camera uses the capture orbit rotation"), viewRotation.Equals(_camera->GetActorRotation(), 0.01));
						_test.AddInfo(FString::Printf(TEXT("Runtime view %s: camera %s, actual view %s, target %s, world rendering disabled %d."), *_parameters, *_camera->GetActorLocation().ToString(), *viewLocation.ToString(), *GetNameSafe(_controller->GetViewTarget()), GEngine->GameViewport->bDisableWorldRendering));
						FScreenshotRequest::RequestScreenshot(_screenshot, false, false, false, FIntRect(), true);
						_phase = 4;
					}
					break;
				}
				case 4:
				{
					if (IFileManager::Get().FileSize(*_screenshot) <= 0)
					{
						break;
					}
					if (_angle == 0)
					{
						_angle = 32;
					}
					else if ((_angle == 32) && (_fullOrbit || FParse::Param(FCommandLine::Get(), TEXT("AppearanceCaptureProfile"))))
					{
						_angle = 90;
					}
					else if ((_angle == 90) && _fullOrbit)
					{
						_angle = 148;
					}
					else
					{
						_angle = 0;
						_motion++;
					}
					if (_motion == 3)
					{
						cleanup();
						return true;
					}
					_phase = 2;
					break;
				}
				default:
				{
					_test.AddError(TEXT("Invalid runtime capture phase."));
					cleanup();
					return true;
				}
			}
			return false;
		}

	private:
		bool verifyWardrobeCloth(const FString& screenshotPath)
		{
			if (!_clothVerify && !FParse::Param(FCommandLine::Get(), TEXT("WardrobeCollisionPose")))
			{
				return true;
			}

			FString captureDirectory;
			if (!FParse::Value(FCommandLine::Get(), TEXT("AppearanceCaptureDir="), captureDirectory) || captureDirectory.IsEmpty())
			{
				_test.AddError(TEXT("WardrobeClothVerify requires an explicit AppearanceCaptureDir."));
				return false;
			}

			const FString folder = _output / (_clothVerify ? TEXT("WardrobeCloth") : TEXT("WardrobeCollisionPose")) / FPaths::GetBaseFilename(screenshotPath);
			if (IFileManager::Get().DirectoryExists(*folder))
			{
				_test.AddError(TEXT("Cloth proof target already exists; use a fresh AppearanceCaptureDir: ") + folder);
				return false;
			}

			if (!IFileManager::Get().MakeDirectory(*folder, true))
			{
				_test.AddError(TEXT("Cannot create cloth proof directory: ") + folder);
				return false;
			}

			const TSharedRef<FJsonObject> report = MakeShared<FJsonObject>();
			report->SetStringField(TEXT("case"), _parameters);
			report->SetStringField(TEXT("motion"), motionName());
			const bool continueClothErrors = _clothVerify && FParse::Param(FCommandLine::Get(), TEXT("WardrobeContinueClothErrors"));
			report->SetBoolField(TEXT("diagnosticContinueClothErrors"), continueClothErrors);
			report->SetNumberField(TEXT("bodyActiveMorphTargets"), _body->ActiveMorphTargets.Num());
			report->SetStringField(TEXT("units"), TEXT("World-space centimetres, little-endian float32 xyz snapshots in physical-mesh vertex order."));
			report->SetNumberField(TEXT("pinnedToleranceCm"), 0.01);
			report->SetNumberField(TEXT("kinematicDistanceThresholdCm"), 0.1);
			report->SetNumberField(TEXT("maxDistanceToleranceCm"), 0.1);
			TArray<TSharedPtr<FJsonValue>> entries;
			bool passed = true;
			TArray<FMatrix44f> bodyPose;
			for (const FTransform& transform : _body->GetComponentSpaceTransforms())
			{
				bodyPose.Add(FMatrix44f(transform.ToMatrixWithScale()));
			}

			const FMatrix44f bodyToWorld(_body->GetComponentTransform().ToMatrixWithScale());
			passed &= _test.TestTrue(TEXT("Cloth collision body pose saves"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(bodyPose.GetData()), bodyPose.Num() * sizeof(FMatrix44f)), *(folder / TEXT("body.cs"))));
			passed &= _test.TestTrue(TEXT("Cloth collision body transform saves"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(&bodyToWorld), sizeof(FMatrix44f)), *(folder / TEXT("body.to_world"))));
			passed &= _test.TestTrue(TEXT("Cloth collision body identity saves"), FFileHelper::SaveStringToFile(_body->GetSkeletalMeshAsset()->GetPathName(), *(folder / TEXT("body.asset")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
			if (FPaths::GetBaseFilename(screenshotPath).EndsWith(TEXT("Standing_Front")))
			{
				TArray<FMatrix44f> facePose;
				for (const FTransform& transform : _face->GetComponentSpaceTransforms())
				{
					facePose.Add(FMatrix44f(transform.ToMatrixWithScale()));
				}

				TArray<FString> faceNames;
				const FReferenceSkeleton& faceReference = _face->GetSkeletalMeshAsset()->GetRefSkeleton();
				for (int32 bone = 0; bone < faceReference.GetNum(); bone++)
				{
					faceNames.Add(faceReference.GetBoneName(bone).ToString());
				}

				const FMatrix44f faceToWorld(_face->GetComponentTransform().ToMatrixWithScale());
				passed &= _test.TestTrue(TEXT("Cloth head pose saves"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(facePose.GetData()), facePose.Num() * sizeof(FMatrix44f)), *(folder / TEXT("face.cs"))));
				passed &= _test.TestTrue(TEXT("Cloth head transform saves"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(&faceToWorld), sizeof(FMatrix44f)), *(folder / TEXT("face.to_world"))));
				passed &= _test.TestTrue(TEXT("Cloth head bone names save"), FFileHelper::SaveStringArrayToFile(faceNames, *(folder / TEXT("face.names")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
			}

			if (!_clothVerify)
			{
				return passed;
			}

			for (const TWeakObjectPtr<USkeletalMeshComponent>& garmentPointer : _garments)
			{
				USkeletalMeshComponent* garment = garmentPointer.Get();
				if (!_test.TestNotNull(TEXT("The cloth garment component exists"), garment))
				{
					passed = false;
					continue;
				}

				if (!garment->IsVisible())
				{
					continue;
				}

				USkeletalMesh* mesh = garment->GetSkeletalMeshAsset();
				const FString garmentName = garment->GetName();
				if (garmentName == TEXT("SKM_Villager_Pants"))
				{
					const UMaterialInterface* material = garment->GetMaterial(0);
					passed &= _test.TestTrue(TEXT("The equipped trousers use an effective two-sided material"), material != nullptr && material->IsTwoSided());
				}

				const bool clothRequired = garmentName == TEXT("SKM_Villager_Hat") || garmentName == TEXT("SKM_Villager_Shirt") || garmentName == TEXT("SKM_Villager_Pants") || garmentName == TEXT("SKM_Villager_BreastWrap") || garmentName == TEXT("SKM_Villager_Braies");
				if (mesh->GetMeshClothingAssets().IsEmpty())
				{
					if (clothRequired)
					{
						_test.AddError(garmentName + TEXT(" has no embedded clothing asset."));
						passed = false;
					}
					continue;
				}

				const UChaosClothAssetSKMClothingAsset* parametricBinding = Cast<UChaosClothAssetSKMClothingAsset>(mesh->GetMeshClothingAssets()[0]);
				if (parametricBinding != nullptr)
				{
					garment->WaitForExistingParallelClothSimulation_GameThread();
					const UChaosClothAssetBase* fittedOutfit = parametricBinding->GetAsset();
					const int32 modelIndex = parametricBinding->GetClothSimulationModelIndex();
					if (!_test.TestNotNull(garmentName + TEXT(" retains its fitted parametric outfit"), fittedOutfit) || !_test.TestTrue(garmentName + TEXT(" has exactly one valid outfit simulation binding"), mesh->GetMeshClothingAssets().Num() == 1 && modelIndex != INDEX_NONE))
					{
						passed = false;
						continue;
					}

					const TSharedPtr<const FChaosClothSimulationModel> model = fittedOutfit->GetClothSimulationModel(modelIndex);
					const TArray<TSharedRef<const FManagedArrayCollection>>& collections = fittedOutfit->GetCollections(modelIndex);
					const FClothSimulData* data = garment->GetCurrentClothingData_GameThread().Find(0);
					if (!_test.TestTrue(garmentName + TEXT(" retains physical LOD0 data and publishes simulated positions"), model.IsValid() && model->IsValidLodIndex(0) && collections.IsValidIndex(0) && data != nullptr))
					{
						passed = false;
						continue;
					}

					const FChaosClothSimulationLodModel& lod = model->ClothSimulationLodModels[0];
					const int32 particleCount = lod.Positions.Num();
					const Chaos::Softs::FCollectionPropertyConstFacade properties(collections[0]);
					const int32 maxDistanceKey = properties.GetKeyIndex(TEXT("MaxDistance"));
					if (!_test.TestTrue(garmentName + TEXT(" has compatible particle buffers and a maximum-distance property"), particleCount > 0 && lod.Normals.Num() == particleCount && lod.BoneData.Num() == particleCount && data->Positions.Num() == particleCount && data->Normals.Num() == particleCount && data->LODIndex == 0 && maxDistanceKey != INDEX_NONE))
					{
						passed = false;
						continue;
					}

					const FVector2f distanceRange = properties.GetWeightedFloatValue(maxDistanceKey);
					const FString weightMapName = properties.GetStringValue(maxDistanceKey);
					const TArray<float>* weights = lod.WeightMaps.Find(FName(*weightMapName));
					if (!_test.TestTrue(garmentName + TEXT(" retains its per-particle maximum-distance map"), weights != nullptr && weights->Num() == particleCount))
					{
						passed = false;
						continue;
					}

					TArray<FMatrix44f> matrices;
					garment->GetCurrentRefToLocalMatrices(matrices, 0, &lod.RequiredExtraBoneIndices);
					TArray<int32> usedBones;
					for (const FName bone : model->UsedBoneNames)
					{
						usedBones.Add(mesh->GetRefSkeleton().FindBoneIndex(bone));
					}

					bool validWeights = true;
					for (const FClothVertBoneData& boneData : lod.BoneData)
					{
						validWeights &= boneData.NumInfluences > 0 && boneData.NumInfluences <= FClothVertBoneData::MaxTotalInfluences;
						for (int32 influence = 0; validWeights && influence < boneData.NumInfluences; influence++)
						{
							const uint16 bone = boneData.BoneIndices[influence];
							validWeights &= usedBones.IsValidIndex(bone) && matrices.IsValidIndex(usedBones[bone]) && FMath::IsFinite(boneData.BoneWeights[influence]);
						}
					}

					if (!_test.TestTrue(garmentName + TEXT(" maps physical skinning to the actual game skeleton"), validWeights))
					{
						passed = false;
						continue;
					}

					FClothPhysicalMeshData physical;
					physical.Vertices = lod.Positions;
					physical.Normals = lod.Normals;
					physical.BoneData = lod.BoneData;
					TArray<FVector3f> targets;
					TArray<FVector3f> normals;
					ClothingMeshUtils::SkinPhysicsMesh(usedBones, physical, FTransform::Identity, matrices.GetData(), matrices.Num(), targets, normals);
					int32 simulatedCloths = 0;
					int32 dynamicParticles = 0;
					float simulationTime = 0.0f;
					TArray<TSharedPtr<FJsonValue>> solverStates;
					for (FClothingSimulationInstance& instance : garment->GetClothingSimulationInstances())
					{
						IClothingSimulationInterface* simulation = instance.GetClothingSimulation();
						const UClothingSimulationFactory* factory = instance.GetClothingSimulationFactory();
						if (!_test.TestTrue(garmentName + TEXT(" uses the modern Chaos cloth asset simulation factory"), simulation != nullptr && factory != nullptr && factory->IsA<UChaosClothAssetSKMClothingSimulationFactory>()))
						{
							passed = false;
							continue;
						}

						simulatedCloths += simulation->GetNumCloths();
						dynamicParticles += simulation->GetNumDynamicParticles();
						simulationTime += simulation->GetSimulationTime();
						UClothingSimulationInteractor* interactor = instance.GetClothingSimulationInteractor();
						if (interactor != nullptr)
						{
							const TSharedRef<FJsonObject> solverState = MakeShared<FJsonObject>();
							solverState->SetNumberField(TEXT("iterations"), interactor->GetNumIterations());
							solverState->SetNumberField(TEXT("substeps"), interactor->GetNumSubsteps());
							solverStates.Add(MakeShared<FJsonValueObject>(solverState));
						}
					}

					const FTransform componentTransform = garment->GetComponentTransform();
					const double maxScale = componentTransform.GetScale3D().GetAbsMax();
					TArray<FVector3f> simulatedWorld;
					TArray<FVector3f> targetWorld;
					TArray<float> maxDistances;
					int32 pinnedCount = 0;
					int32 dynamicCount = 0;
					double maxPinnedError = 0.0;
					double maxDynamicDeparture = 0.0;
					double maxDistanceViolation = 0.0;
					bool finite = !data->Transform.ContainsNaN();
					for (int32 particle = 0; particle < particleCount; particle++)
					{
						const FVector simulated = data->Transform.TransformPosition(FVector(data->Positions[particle]));
						const FVector target = componentTransform.TransformPosition(FVector(targets[particle]));
						const float maxDistance = FMath::Lerp(distanceRange.X, distanceRange.Y, (*weights)[particle]);
						const double departure = FVector::Distance(simulated, target);
						finite &= !simulated.ContainsNaN() && !target.ContainsNaN() && FMath::IsFinite(maxDistance) && maxDistance >= 0.0f;
						if (maxDistance < 0.1f)
						{
							pinnedCount++;
							maxPinnedError = FMath::Max(maxPinnedError, departure);
						}
						else
						{
							dynamicCount++;
							maxDynamicDeparture = FMath::Max(maxDynamicDeparture, departure);
							maxDistanceViolation = FMath::Max(maxDistanceViolation, departure - maxDistance * maxScale);
						}

						simulatedWorld.Add(FVector3f(simulated));
						targetWorld.Add(FVector3f(target));
						maxDistances.Add(maxDistance);
					}

					passed &= _test.TestTrue(garmentName + TEXT(" publishes a live solver state"), !solverStates.IsEmpty());
					passed &= _test.TestTrue(garmentName + TEXT(" runs modern cloth with finite pinned and dynamic particles"), finite && simulatedCloths > 0 && dynamicParticles > 0 && pinnedCount > 0 && dynamicCount > 0 && simulationTime > 0.0f && FMath::IsFinite(simulationTime));
					passed &= _test.TestTrue(garmentName + TEXT(" renders its own fitted outfit simulation"), garment->bWaitForParallelClothTask && !garment->bDisableClothSimulation && !garment->IsClothBoundToLeaderComponent() && garment->ClothBlendWeight > 0.0f && mesh->HasActiveClothingAssetsForLOD(0));
					passed &= _test.TestTrue(garmentName + TEXT(" keeps pinned particles on targets within 0.01 cm"), maxPinnedError < 0.01);
					passed &= _test.TestTrue(garmentName + TEXT(" respects maximum distance within 0.1 cm"), maxDistanceViolation <= 0.1);
					if (_motion != 0)
					{
						passed &= _test.TestTrue(garmentName + TEXT(" actually simulates during movement"), maxDynamicDeparture > 0.01);
					}

					const FString stem = garmentName + TEXT("_0");
					passed &= _test.TestTrue(TEXT("Parametric simulated particles save"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(simulatedWorld.GetData()), simulatedWorld.Num() * sizeof(FVector3f)), *(folder / (stem + TEXT(".sim_world")))));
					passed &= _test.TestTrue(TEXT("Parametric animated targets save"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(targetWorld.GetData()), targetWorld.Num() * sizeof(FVector3f)), *(folder / (stem + TEXT(".target_world")))));
					passed &= _test.TestTrue(TEXT("Parametric maximum distances save"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(maxDistances.GetData()), maxDistances.Num() * sizeof(float)), *(folder / (stem + TEXT(".max")))));
					const FMatrix44f simulationReference(data->Transform.ToMatrixWithScale());
					const FMatrix44f garmentToWorld(componentTransform.ToMatrixWithScale());
					passed &= _test.TestTrue(TEXT("Parametric actual reference normals save"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(data->Normals.GetData()), data->Normals.Num() * sizeof(FVector3f)), *(folder / (stem + TEXT(".sim_normal_reference")))));
					passed &= _test.TestTrue(TEXT("Parametric simulation reference transform saves"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(&simulationReference), sizeof(FMatrix44f)), *(folder / (stem + TEXT(".sim_reference_to_world")))));
					passed &= _test.TestTrue(TEXT("Parametric target component normals save"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(normals.GetData()), normals.Num() * sizeof(FVector3f)), *(folder / (stem + TEXT(".target_normal_component")))));
					passed &= _test.TestTrue(TEXT("Parametric garment transform saves"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(&garmentToWorld), sizeof(FMatrix44f)), *(folder / (stem + TEXT(".garment_to_world")))));
					const TSharedRef<FJsonObject> entry = MakeShared<FJsonObject>();
					entry->SetStringField(TEXT("normalFormat"), TEXT("Little-endian float32[N,3]; actual normals are in simulation reference space, target normals in garment component space; transforms are row-vector float32[4,4]."));
					entry->SetStringField(TEXT("garment"), mesh->GetPathName());
					entry->SetStringField(TEXT("clothAsset"), parametricBinding->GetPathName());
					entry->SetStringField(TEXT("fittedOutfit"), fittedOutfit->GetPathName());
					entry->SetStringField(TEXT("snapshotStem"), stem);
					entry->SetNumberField(TEXT("particles"), particleCount);
					entry->SetNumberField(TEXT("pinnedParticles"), pinnedCount);
					entry->SetNumberField(TEXT("dynamicParticles"), dynamicCount);
					entry->SetNumberField(TEXT("simulationTime"), simulationTime);
					entry->SetArrayField(TEXT("solverStates"), solverStates);
					entry->SetNumberField(TEXT("maxPinnedErrorCm"), maxPinnedError);
					entry->SetNumberField(TEXT("maxDynamicDepartureCm"), maxDynamicDeparture);
					entry->SetNumberField(TEXT("maxDistanceViolationCm"), maxDistanceViolation);
					entry->SetBoolField(TEXT("finite"), finite);
					entries.Add(MakeShared<FJsonValueObject>(entry));
					continue;
				}

				garment->WaitForExistingParallelClothSimulation_GameThread();
				TArray<FClothingSimulationInstance>& instances = garment->GetClothingSimulationInstances();
				const TArray<TObjectPtr<UClothingAssetBase>>& clothingAssets = mesh->GetMeshClothingAssets();
				const FReferenceSkeleton& collisionReference = mesh->GetRefSkeleton();
				int32 simulatedCloths = 0;
				int32 dynamicParticles = 0;
				float simulationTime = 0.0f;
				const TSharedRef<FJsonObject> collisionReport = MakeShared<FJsonObject>();
				const FVector componentScale = garment->GetComponentScale();
				collisionReport->SetStringField(TEXT("garment"), mesh->GetPathName());
				collisionReport->SetArrayField(TEXT("componentScale"), { MakeShared<FJsonValueNumber>(componentScale.X), MakeShared<FJsonValueNumber>(componentScale.Y), MakeShared<FJsonValueNumber>(componentScale.Z) });
				collisionReport->SetStringField(TEXT("units"), TEXT("World centimetres; solver geometry converted using only captured particle rotation, translation, solver scale and origin."));
				collisionReport->SetStringField(TEXT("indexMapping"), TEXT("geometryIndex indexes the LODless solver range, not PhysicsAsset element arrays. Source mesh bone indices are read from the solver. Engine order is sphere connections, unconnected spheres, convexes, boxes, extruded capsules then level sets."));
				TArray<TSharedPtr<FJsonValue>> collisionGroups;
				bool collisionCapturePassed = true;
				const TFunction<TArray<TSharedPtr<FJsonValue>>(const FVector&)> vectorJson = [](const FVector& value)
				{
					return TArray<TSharedPtr<FJsonValue>> { MakeShared<FJsonValueNumber>(value.X), MakeShared<FJsonValueNumber>(value.Y), MakeShared<FJsonValueNumber>(value.Z) };
				};
				for (int32 instanceIndex = 0; instanceIndex < instances.Num(); instanceIndex++)
				{
					FClothingSimulationInstance& instance = instances[instanceIndex];
					IClothingSimulationInterface* simulation = instance.GetClothingSimulation();
					if (simulation == nullptr)
					{
						collisionCapturePassed = false;
						continue;
					}

					simulatedCloths += simulation->GetNumCloths();
					dynamicParticles += simulation->GetNumDynamicParticles();
					simulationTime += simulation->GetSimulationTime();
					const UClothingSimulationFactory* factory = instance.GetClothingSimulationFactory();
					if (factory == nullptr || !factory->IsA<UChaosClothingSimulationFactory>())
					{
						_test.AddError(garmentName + TEXT(" requires the Chaos factory for solver collision capture."));
						collisionCapturePassed = false;
						continue;
					}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
					Chaos::FClothingSimulation* chaosSimulation = static_cast<Chaos::FClothingSimulation*>(simulation);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
					const Chaos::FClothingSimulationSolver* solver = chaosSimulation->GetSolver();
					if (solver == nullptr)
					{
						collisionCapturePassed = false;
						continue;
					}

					const FVector solverOrigin(solver->GetLocalSpaceLocation());
					const double solverScale = solver->GetLocalSpaceScale();
					collisionCapturePassed &= !solverOrigin.ContainsNaN() && FMath::IsFinite(solverScale) && solverScale > 0.0;
					for (const Chaos::FClothingSimulationCloth* cloth : solver->GetCloths())
					{
						const TSharedRef<FJsonObject> group = MakeShared<FJsonObject>();
						group->SetNumberField(TEXT("instanceIndex"), instanceIndex);
						group->SetNumberField(TEXT("clothGroupId"), cloth->GetGroupId());
						group->SetArrayField(TEXT("solverOriginWorld"), vectorJson(solverOrigin));
						group->SetNumberField(TEXT("solverToWorldScale"), solverScale);
						group->SetBoolField(TEXT("legacySolver"), solver->IsLegacySolver());
						const int32 groupId = static_cast<int32>(cloth->GetGroupId());
						const UClothingAssetCommon* sourceAsset = clothingAssets.IsValidIndex(groupId) ? Cast<UClothingAssetCommon>(clothingAssets[groupId]) : nullptr;
						group->SetStringField(TEXT("sourceClothAsset"), sourceAsset != nullptr ? sourceAsset->GetPathName() : TEXT(""));
						group->SetStringField(TEXT("sourcePhysicsAsset"), sourceAsset != nullptr && sourceAsset->PhysicsAsset != nullptr ? sourceAsset->PhysicsAsset->GetPathName() : TEXT(""));
						TArray<TSharedPtr<FJsonValue>> shapes;
						int32 colliderIndex = 0;
						for (const Chaos::FClothingSimulationCollider* collider : cloth->GetColliders())
						{
							const Chaos::FClothingSimulationCollider::ECollisionDataType slot = Chaos::FClothingSimulationCollider::ECollisionDataType::LODless;
							const int32 rangeId = collider->GetCollisionRangeId(solver, cloth, slot);
							if (rangeId == INDEX_NONE)
							{
								colliderIndex++;
								continue;
							}

							const TConstArrayView<Chaos::Softs::FSolverVec3> positions = collider->GetCollisionTranslations(solver, cloth, slot);
							const TConstArrayView<Chaos::Softs::FSolverRotation3> rotations = collider->GetCollisionRotations(solver, cloth, slot);
							const TConstArrayView<Chaos::FImplicitObjectPtr> geometry = collider->GetCollisionGeometry(solver, cloth, slot);
							const TConstArrayView<int32> bones = solver->GetCollisionBoneIndicesView(rangeId);
							if (positions.Num() != geometry.Num() || rotations.Num() != geometry.Num() || bones.Num() != geometry.Num())
							{
								_test.AddError(garmentName + TEXT(" solver collision buffers have different sizes."));
								collisionCapturePassed = false;
								colliderIndex++;
								continue;
							}

							for (int32 geometryIndex = 0; geometryIndex < geometry.Num(); geometryIndex++)
							{
								const TSharedRef<FJsonObject> shape = MakeShared<FJsonObject>();
								const FVector position(positions[geometryIndex]);
								const FQuat rotation(rotations[geometryIndex]);
								shape->SetNumberField(TEXT("colliderIndex"), colliderIndex);
								shape->SetNumberField(TEXT("rangeId"), rangeId);
								shape->SetNumberField(TEXT("geometryIndex"), geometryIndex);
								shape->SetNumberField(TEXT("meshBoneIndex"), bones[geometryIndex]);
								shape->SetStringField(TEXT("bone"), collisionReference.IsValidIndex(bones[geometryIndex]) ? collisionReference.GetBoneName(bones[geometryIndex]).ToString() : TEXT(""));
								shape->SetArrayField(TEXT("particlePositionSolver"), vectorJson(position));
								shape->SetArrayField(TEXT("particleRotationXYZW"), { MakeShared<FJsonValueNumber>(rotation.X), MakeShared<FJsonValueNumber>(rotation.Y), MakeShared<FJsonValueNumber>(rotation.Z), MakeShared<FJsonValueNumber>(rotation.W) });
								const Chaos::FImplicitObject* implicit = geometry[geometryIndex].GetReference();
								if (implicit == nullptr)
								{
									shape->SetStringField(TEXT("type"), TEXT("null"));
									collisionCapturePassed = false;
								}
								else if (implicit->IsA<Chaos::FCapsule>())
								{
									const Chaos::FCapsule& capsule = implicit->GetObjectChecked<Chaos::FCapsule>();
									shape->SetStringField(TEXT("type"), TEXT("capsule"));
									shape->SetArrayField(TEXT("endpoint0World"), vectorJson(solverOrigin + solverScale * (position + rotation.RotateVector(FVector(capsule.GetX1f())))));
									shape->SetArrayField(TEXT("endpoint1World"), vectorJson(solverOrigin + solverScale * (position + rotation.RotateVector(FVector(capsule.GetX2f())))));
									shape->SetNumberField(TEXT("radiusWorldCm"), solverScale * capsule.GetRadiusf());
								}
								else if (implicit->IsA<Chaos::TSphere<Chaos::FReal, 3>>())
								{
									const Chaos::TSphere<Chaos::FReal, 3>& sphere = implicit->GetObjectChecked<Chaos::TSphere<Chaos::FReal, 3>>();
									shape->SetStringField(TEXT("type"), TEXT("sphere"));
									shape->SetArrayField(TEXT("centerWorld"), vectorJson(solverOrigin + solverScale * (position + rotation.RotateVector(FVector(sphere.GetCenterf())))));
									shape->SetNumberField(TEXT("radiusWorldCm"), solverScale * sphere.GetRadiusf());
								}
								else if (implicit->IsA<Chaos::TBox<Chaos::FReal, 3>>())
								{
									const Chaos::TBox<Chaos::FReal, 3>& box = implicit->GetObjectChecked<Chaos::TBox<Chaos::FReal, 3>>();
									shape->SetStringField(TEXT("type"), TEXT("box"));
									shape->SetArrayField(TEXT("centerWorld"), vectorJson(solverOrigin + solverScale * (position + rotation.RotateVector(FVector((box.Min() + box.Max()) * 0.5)))));
									shape->SetArrayField(TEXT("halfExtentsWorldCm"), vectorJson(solverScale * FVector((box.Max() - box.Min()) * 0.5)));
									shape->SetArrayField(TEXT("rotationWorldXYZW"), { MakeShared<FJsonValueNumber>(rotation.X), MakeShared<FJsonValueNumber>(rotation.Y), MakeShared<FJsonValueNumber>(rotation.Z), MakeShared<FJsonValueNumber>(rotation.W) });
								}
								else
								{
									shape->SetStringField(TEXT("type"), implicit->GetTypeName().ToString());
									_test.AddError(garmentName + TEXT(" has an unsupported solver collision geometry type: ") + implicit->GetTypeName().ToString());
									collisionCapturePassed = false;
								}

								shapes.Add(MakeShared<FJsonValueObject>(shape));
							}

							colliderIndex++;
						}

						group->SetArrayField(TEXT("shapes"), shapes);
						collisionGroups.Add(MakeShared<FJsonValueObject>(group));
					}
				}

				collisionReport->SetArrayField(TEXT("groups"), collisionGroups);
				collisionReport->SetBoolField(TEXT("complete"), collisionCapturePassed);
				FString collisionJson;
				const TSharedRef<TJsonWriter<>> collisionWriter = TJsonWriterFactory<>::Create(&collisionJson);
				const bool serializedCollisions = FJsonSerializer::Serialize(collisionReport, collisionWriter);
				passed &= _test.TestTrue(garmentName + TEXT(" captures supported actual solver collisions"), collisionCapturePassed);
				passed &= _test.TestTrue(garmentName + TEXT(" saves solver collision snapshots"), serializedCollisions && FFileHelper::SaveStringToFile(collisionJson, *(folder / (garmentName + TEXT(".colliders.json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

				passed &= _test.TestTrue(garmentName + TEXT(" runs a cloth simulation with dynamic particles"), simulatedCloths > 0 && dynamicParticles > 0 && simulationTime > 0.0f && FMath::IsFinite(simulationTime));
				passed &= _test.TestTrue(garmentName + TEXT(" renders its own cloth simulation"), garment->bWaitForParallelClothTask && !garment->bDisableClothSimulation && !garment->IsClothBoundToLeaderComponent() && garment->ClothBlendWeight > 0.0f && mesh->HasActiveClothingAssetsForLOD(0));
				const TMap<int32, FClothSimulData>& simulationData = garment->GetCurrentClothingData_GameThread();
				TArray<FMatrix44f> matrices;
				garment->GetCurrentRefToLocalMatrices(matrices, 0);
				const FTransform componentTransform = garment->GetComponentTransform();
				const double maxScale = componentTransform.GetScale3D().GetAbsMax();
				for (int32 assetIndex = 0; assetIndex < clothingAssets.Num(); assetIndex++)
				{
					const UClothingAssetCommon* asset = Cast<UClothingAssetCommon>(clothingAssets[assetIndex]);
					if (!_test.TestNotNull(garmentName + TEXT(" has common physical cloth data"), asset))
					{
						passed = false;
						continue;
					}

					if (asset->LodMap.IsEmpty() || !asset->LodData.IsValidIndex(asset->LodMap[0]))
					{
						_test.AddError(garmentName + TEXT(" has no valid LOD0 physical cloth binding."));
						passed = false;
						continue;
					}

					const FClothSimulData* data = simulationData.Find(assetIndex);
					if (!_test.TestNotNull(garmentName + TEXT(" publishes actual simulated particle positions"), data))
					{
						passed = false;
						continue;
					}

					const FClothPhysicalMeshData& physical = asset->LodData[asset->LodMap[0]].PhysicalMeshData;
					const FPointWeightMap* maxDistances = physical.FindWeightMap(EWeightMapTargetCommon::MaxDistance);
					const int32 particleCount = physical.Vertices.Num();
					if (maxDistances == nullptr || particleCount == 0 || maxDistances->Num() != particleCount || data->Positions.Num() != particleCount || physical.BoneData.Num() != particleCount || physical.Normals.Num() != particleCount || data->Transform.ContainsNaN() || data->LODIndex != 0)
					{
						_test.AddError(garmentName + TEXT(" has incompatible physical targets and simulation buffers."));
						passed = false;
						continue;
					}

					bool validWeights = true;
					for (const FClothVertBoneData& boneData : physical.BoneData)
					{
						if (boneData.NumInfluences < 1 || boneData.NumInfluences > FClothVertBoneData::MaxTotalInfluences)
						{
							validWeights = false;
							break;
						}

						for (int32 influence = 0; influence < boneData.NumInfluences; influence++)
						{
							const uint16 bone = boneData.BoneIndices[influence];
							if (!asset->UsedBoneIndices.IsValidIndex(bone))
							{
								validWeights = false;
								break;
							}

							validWeights &= matrices.IsValidIndex(asset->UsedBoneIndices[bone]) && FMath::IsFinite(boneData.BoneWeights[influence]);
						}
					}

					if (!_test.TestTrue(garmentName + TEXT(" has valid physical skinning indices and weights"), validWeights))
					{
						passed = false;
						continue;
					}

					TArray<FVector3f> targets;
					TArray<FVector3f> normals;
					ClothingMeshUtils::SkinPhysicsMesh(asset->UsedBoneIndices, physical, FTransform::Identity, matrices.GetData(), matrices.Num(), targets, normals);
					TArray<FVector3f> simulatedWorld;
					TArray<FVector3f> targetWorld;
					simulatedWorld.Reserve(particleCount);
					targetWorld.Reserve(particleCount);
					int32 pinnedCount = 0;
					int32 dynamicCount = 0;
					double maxPinnedError = 0.0;
					double maxDynamicDeparture = 0.0;
					double maxDistanceViolation = 0.0;
					bool finite = true;
					for (int32 particle = 0; particle < particleCount; particle++)
					{
						const FVector simulated = data->Transform.TransformPosition(FVector(data->Positions[particle]));
						const FVector target = componentTransform.TransformPosition(FVector(targets[particle]));
						const float maxDistance = maxDistances->Values[particle];
						finite &= !simulated.ContainsNaN() && !target.ContainsNaN() && FMath::IsFinite(maxDistance) && maxDistance >= 0.0f;
						const double departure = FVector::Distance(simulated, target);
						if (maxDistance < 0.1f)
						{
							pinnedCount++;
							maxPinnedError = FMath::Max(maxPinnedError, departure);
						}
						else
						{
							dynamicCount++;
							maxDynamicDeparture = FMath::Max(maxDynamicDeparture, departure);
							maxDistanceViolation = FMath::Max(maxDistanceViolation, departure - maxDistance * maxScale);
						}

						simulatedWorld.Add(FVector3f(simulated));
						targetWorld.Add(FVector3f(target));
					}

					passed &= _test.TestTrue(garmentName + TEXT(" publishes finite physical targets and simulated particles"), finite);
					passed &= _test.TestTrue(garmentName + TEXT(" has pinned and dynamic simulated particles"), pinnedCount > 0 && dynamicCount > 0);
					passed &= _test.TestTrue(garmentName + TEXT(" keeps pinned particles on their animated targets within 0.01 cm"), maxPinnedError < 0.01);
					passed &= _test.TestTrue(garmentName + TEXT(" respects per-particle maximum distance within 0.1 cm"), maxDistanceViolation <= 0.1);
					if (_motion != 0)
					{
						passed &= _test.TestTrue(garmentName + TEXT(" visibly simulates beyond the animated skinning target during movement"), maxDynamicDeparture > 0.01);
					}

					const FString stem = garmentName + FString::Printf(TEXT("_%d"), assetIndex);
					passed &= _test.TestTrue(TEXT("Simulated particle snapshot saves"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(simulatedWorld.GetData()), simulatedWorld.Num() * sizeof(FVector3f)), *(folder / (stem + TEXT(".sim_world")))));
					passed &= _test.TestTrue(TEXT("Animated physical target snapshot saves"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(targetWorld.GetData()), targetWorld.Num() * sizeof(FVector3f)), *(folder / (stem + TEXT(".target_world")))));
					passed &= _test.TestTrue(TEXT("Maximum-distance snapshot saves"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(maxDistances->Values.GetData()), maxDistances->Num() * sizeof(float)), *(folder / (stem + TEXT(".max")))));
					passed &= _test.TestTrue(TEXT("Animated physical normal snapshot saves"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(normals.GetData()), normals.Num() * sizeof(FVector3f)), *(folder / (stem + TEXT(".target_component_normal")))));
					const TSharedRef<FJsonObject> entry = MakeShared<FJsonObject>();
					entry->SetStringField(TEXT("garment"), mesh->GetPathName());
					entry->SetStringField(TEXT("clothAsset"), asset->GetPathName());
					entry->SetStringField(TEXT("snapshotStem"), stem);
					entry->SetNumberField(TEXT("particles"), particleCount);
					entry->SetNumberField(TEXT("pinnedParticles"), pinnedCount);
					entry->SetNumberField(TEXT("dynamicParticles"), dynamicCount);
					entry->SetNumberField(TEXT("simulationInstances"), instances.Num());
					entry->SetNumberField(TEXT("simulationTime"), simulationTime);
					entry->SetNumberField(TEXT("maxPinnedErrorCm"), maxPinnedError);
					entry->SetNumberField(TEXT("maxDynamicDepartureCm"), maxDynamicDeparture);
					entry->SetNumberField(TEXT("maxDistanceViolationCm"), maxDistanceViolation);
					entry->SetNumberField(TEXT("maxDistanceWorldScale"), maxScale);
					entry->SetBoolField(TEXT("finite"), finite);
					entries.Add(MakeShared<FJsonValueObject>(entry));
				}
			}

			report->SetArrayField(TEXT("cloth"), entries);
			report->SetBoolField(TEXT("passed"), passed);
			FString json;
			const TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&json);
			FJsonSerializer::Serialize(report, writer);
			passed &= _test.TestTrue(TEXT("The cloth verification report saves"), FFileHelper::SaveStringToFile(json, *(folder / TEXT("proof.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
			_test.AddInfo(TEXT("WARDROBE_CLOTH ") + folder);
			return passed || continueClothErrors;
		}

		bool dumpWardrobePose(const FString& screenshotPath)
		{
			if (!FParse::Param(FCommandLine::Get(), TEXT("WardrobeDumpPose")))
			{
				return true;
			}

			FString captureDirectory;
			if (!FParse::Value(FCommandLine::Get(), TEXT("AppearanceCaptureDir="), captureDirectory) || captureDirectory.IsEmpty())
			{
				_test.AddError(TEXT("WardrobeDumpPose requires an explicit AppearanceCaptureDir."));
				return false;
			}

			const FString folder = _output / TEXT("WardrobePose") / FPaths::GetBaseFilename(screenshotPath);
			if (IFileManager::Get().DirectoryExists(*folder))
			{
				_test.AddError(TEXT("Pose dump target already exists; use a fresh AppearanceCaptureDir: ") + folder);
				return false;
			}

			if (!_test.TestTrue(TEXT("The pose dump directory is created"), IFileManager::Get().MakeDirectory(*folder, true)))
			{
				return false;
			}

			const FReferenceSkeleton& bodyReference = _body->GetSkeletalMeshAsset()->GetRefSkeleton();
			const TArray<FTransform>& componentPose = _body->GetComponentSpaceTransforms();
			const int32 boneCount = bodyReference.GetRawBoneNum();
			if (!_test.TestEqual(TEXT("The pose dump contains 342 reference bones"), boneCount, 342))
			{
				return false;
			}

			if (!_test.TestTrue(TEXT("The current component pose contains every reference bone"), componentPose.Num() >= boneCount))
			{
				return false;
			}

			TArray<FString> names;
			names.Reserve(boneCount);
			TArray<FMatrix44f> matrices;
			matrices.Reserve(boneCount);
			for (int32 index = 0; index < boneCount; index++)
			{
				names.Add(bodyReference.GetBoneName(index).ToString());
				matrices.Add(FMatrix44f(componentPose[index].ToMatrixWithScale()));
			}

			if (!_test.TestTrue(TEXT("The body pose names save"), FFileHelper::SaveStringArrayToFile(names, *(folder / TEXT("body.names")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
			{
				return false;
			}

			if (!_test.TestTrue(TEXT("The body component matrices save"), FFileHelper::SaveArrayToFile(MakeArrayView(reinterpret_cast<const uint8*>(matrices.GetData()), matrices.Num() * sizeof(FMatrix44f)), *(folder / TEXT("body.cs")))))
			{
				return false;
			}

			TArray<FString> metadata = { TEXT("format=AlverionWardrobePose1"), TEXT("units=cm"), TEXT("byte_order=little_endian"), TEXT("matrix_type=float32[4,4]"), TEXT("matrix_order=row_major,row_vector,translation_row3"), TEXT("pose_space=body_component"), TEXT("vertex_space=garment_component"), TEXT("vertex_order=LOD0_render_vertex"), TEXT("positions=float32[N,3]"), TEXT("triangles=uint32[T,3],global_render_indices"), TEXT("uv0=float32[N,2]"), TEXT("globalweights=uint16[N,K,2],body.names_index,weight_divided_by_65535"), FString::Printf(TEXT("bone_count=%d"), boneCount), TEXT("body_asset=") + _body->GetSkeletalMeshAsset()->GetPathName() };
			for (const TWeakObjectPtr<USkeletalMeshComponent>& garment : _garments)
			{
				if (!_test.TestTrue(TEXT("The pose dump garment is valid"), garment.IsValid()))
				{
					return false;
				}

				const FSkeletalMeshRenderData* renderData = garment->GetSkeletalMeshRenderData();
				if (!_test.TestNotNull(TEXT("The pose dump garment has render data"), renderData))
				{
					return false;
				}

				if (!_test.TestTrue(TEXT("The pose dump garment has LOD0"), renderData->LODRenderData.IsValidIndex(0)))
				{
					return false;
				}

				const FSkeletalMeshLODRenderData& lod = renderData->LODRenderData[0];
				const FSkinWeightVertexBuffer* skinWeights = garment->GetSkinWeightBuffer(0);
				if (!_test.TestNotNull(TEXT("The pose dump garment has skin weights"), skinWeights))
				{
					return false;
				}

				if (!_test.TestNotNull(TEXT("The pose dump retains CPU position data"), lod.StaticVertexBuffers.PositionVertexBuffer.GetVertexData()) || !_test.TestNotNull(TEXT("The pose dump retains CPU weight data"), skinWeights->GetDataVertexBuffer()->GetWeightData()) || !_test.TestNotNull(TEXT("The pose dump retains CPU UV data"), lod.StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordData()))
				{
					return false;
				}

				if (!_test.TestTrue(TEXT("The pose dump garment has UV0"), lod.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() > 0) || !_test.TestTrue(TEXT("The pose dump garment has indices"), lod.MultiSizeIndexContainer.IsIndexBufferValid()))
				{
					return false;
				}

				const uint32 vertexCount = lod.GetNumVertices();
				const uint32 influenceCount = skinWeights->GetMaxBoneInfluences();
				TArray<FMatrix44f> refToLocal;
				TArray<FVector3f> positions;
				garment->GetCurrentRefToLocalMatrices(refToLocal, 0);
				USkinnedMeshComponent::ComputeSkinnedPositions(garment.Get(), positions, refToLocal, lod, *skinWeights);
				if (!_test.TestEqual(TEXT("The pose dump skins every garment vertex"), positions.Num(), static_cast<int32>(vertexCount)))
				{
					return false;
				}

				TArray<uint32> indices;
				lod.MultiSizeIndexContainer.GetIndexBuffer(indices);
				if (!_test.TestTrue(TEXT("The pose dump contains complete triangles"), indices.Num() > 0 && indices.Num() % 3 == 0))
				{
					return false;
				}

				TArray<FVector3f> restPositions;
				restPositions.Reserve(vertexCount);
				TArray<FVector2f> uv0;
				uv0.Reserve(vertexCount);
				for (uint32 index = 0; index < vertexCount; index++)
				{
					restPositions.Add(lod.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(index));
					uv0.Add(lod.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(index, 0));
				}

				TArray<uint16> globalWeights;
				globalWeights.SetNumZeroed(vertexCount * influenceCount * 2);
				const FReferenceSkeleton& garmentReference = garment->GetSkeletalMeshAsset()->GetRefSkeleton();
				for (const FSkelMeshRenderSection& section : lod.RenderSections)
				{
					const uint32 vertexEnd = section.BaseVertexIndex + section.NumVertices;
					for (uint32 vertex = section.BaseVertexIndex; vertex < vertexEnd; vertex++)
					{
						for (uint32 influence = 0; influence < influenceCount; influence++)
						{
							const uint16 weight = skinWeights->GetBoneWeight(vertex, influence);
							if (weight == 0)
							{
								continue;
							}

							const uint32 paletteIndex = skinWeights->GetBoneIndex(vertex, influence);
							if (!_test.TestTrue(TEXT("The pose dump weight has a valid section bone"), section.BoneMap.IsValidIndex(paletteIndex)))
							{
								return false;
							}

							const int32 bodyBoneIndex = bodyReference.FindRawBoneIndex(garmentReference.GetBoneName(section.BoneMap[paletteIndex]));
							if (!_test.TestTrue(TEXT("The pose dump weight maps to a body bone"), bodyBoneIndex != INDEX_NONE))
							{
								return false;
							}

							const uint32 offset = (vertex * influenceCount + influence) * 2;
							globalWeights[offset] = static_cast<uint16>(bodyBoneIndex);
							globalWeights[offset + 1] = weight;
						}
					}
				}

				const FMatrix44f componentToBody(garment->GetComponentTransform().GetRelativeTransform(_body->GetComponentTransform()).ToMatrixWithScale());
				const FString stem = folder / garment->GetName();
				TArray<TPair<FString, TConstArrayView<uint8>>> files;
				files.Emplace(stem + TEXT(".v"), MakeArrayView(reinterpret_cast<const uint8*>(positions.GetData()), positions.Num() * sizeof(FVector3f)));
				files.Emplace(stem + TEXT(".refv"), MakeArrayView(reinterpret_cast<const uint8*>(restPositions.GetData()), restPositions.Num() * sizeof(FVector3f)));
				files.Emplace(stem + TEXT(".t"), MakeArrayView(reinterpret_cast<const uint8*>(indices.GetData()), indices.Num() * sizeof(uint32)));
				files.Emplace(stem + TEXT(".uv"), MakeArrayView(reinterpret_cast<const uint8*>(uv0.GetData()), uv0.Num() * sizeof(FVector2f)));
				files.Emplace(stem + TEXT(".globalweights"), MakeArrayView(reinterpret_cast<const uint8*>(globalWeights.GetData()), globalWeights.Num() * sizeof(uint16)));
				files.Emplace(stem + TEXT(".component_to_body"), MakeArrayView(reinterpret_cast<const uint8*>(&componentToBody), sizeof(FMatrix44f)));
				const FSkeletalMeshVertexClothBuffer& clothBuffer = lod.ClothVertexBuffer;
				const uint32 clothRows = clothBuffer.GetNumVertices();
				if (clothRows > 0)
				{
					static_assert(sizeof(FMeshToMeshVertData) == 64);
					static_assert(sizeof(FClothBufferIndexMapping) == 12);
					if (!_test.TestEqual(TEXT("The live cloth mapping stride matches the packed descriptor"), clothBuffer.GetStride(), static_cast<uint32>(sizeof(FMeshToMeshVertData))))
					{
						return false;
					}

					const TArray<FClothBufferIndexMapping>& clothIndices = clothBuffer.GetClothIndexMapping();
					files.Emplace(stem + TEXT(".gpu_cloth_mappings"), MakeArrayView(reinterpret_cast<const uint8*>(&clothBuffer.MappingData(0)), clothRows * sizeof(FMeshToMeshVertData)));
					files.Emplace(stem + TEXT(".gpu_cloth_indices"), MakeArrayView(reinterpret_cast<const uint8*>(clothIndices.GetData()), clothIndices.Num() * sizeof(FClothBufferIndexMapping)));
					metadata.Add(FString::Printf(TEXT("cloth=%s,rows=%u,stride=64,index_rows=%d"), *garment->GetName(), clothRows, clothIndices.Num()));
				}

				for (const TPair<FString, TConstArrayView<uint8>>& file : files)
				{
					if (!_test.TestTrue(TEXT("The garment pose buffer saves: ") + file.Key, FFileHelper::SaveArrayToFile(file.Value, *file.Key)))
					{
						return false;
					}
				}

				metadata.Add(FString::Printf(TEXT("garment=%s,vertices=%u,triangles=%d,K=%u,asset=%s"), *garment->GetName(), vertexCount, indices.Num() / 3, influenceCount, *garment->GetSkeletalMeshAsset()->GetPathName()));
			}

			metadata.Add(TEXT("gpu_cloth_mappings=LE64bytes,position_f32x4@0,normal_f32x4@16,tangent_f32x4@32,source_u16x4@48,weight_f32@56,padding_u32@60"));
			metadata.Add(TEXT("gpu_cloth_indices=LEuint32[N,3],BaseVertexIndex,MappingOffset,LODBiasStride"));
			if (!_test.TestTrue(TEXT("The pose dump format manifest saves"), FFileHelper::SaveStringArrayToFile(metadata, *(folder / TEXT("format.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
			{
				return false;
			}

			_test.AddInfo(TEXT("WARDROBE_POSE ") + folder);
			return true;
		}

		const TCHAR* motionName() const
		{
			return _motion == 0 ? TEXT("Standing") : _motion == 1 ? TEXT("Walking") : TEXT("Running");
		}

		void cleanup()
		{
			if (_controller.IsValid() && _oldView.IsValid())
			{
				_controller->SetViewTarget(_oldView.Get());
			}
			for (const TWeakObjectPtr<AActor>& actor : _owned)
			{
				if (actor.IsValid())
				{
					actor->Destroy();
				}
			}
		}

		FAutomationTestBase& _test;
		FString _parameters;
		FString _donor;
		FString _race;
		FString _output;
		FString _screenshot;
		FString _pieceOnly;
		Network::FAppearance _look;
		TWeakObjectPtr<UWorld> _world;
		TWeakObjectPtr<APlayerController> _controller;
		TWeakObjectPtr<AActor> _oldView;
		TWeakObjectPtr<AProxyCharacter> _proxy;
		TWeakObjectPtr<AActor> _visual;
		TWeakObjectPtr<USkeletalMeshComponent> _body;
		TWeakObjectPtr<USkeletalMeshComponent> _face;
		TWeakObjectPtr<USkeletalMesh> _donorBody;
		TArray<FTransform> _donorReference;
		TArray<TWeakObjectPtr<USkeletalMeshComponent>> _garments;
		TWeakObjectPtr<ACameraActor> _camera;
		TWeakObjectPtr<AStaticMeshActor> _wall;
		TArray<TWeakObjectPtr<APointLight>> _lights;
		TArray<FVector> _lightOffsets;
		TArray<TWeakObjectPtr<AActor>> _owned;
		bool _validPiece = true;
		bool _wardrobePreview = false;
		bool _handCapture = false;
		bool _customCamera = false;
		bool _rearCapture = false;
		bool _fullOrbit = false;
		bool _clothVerify = false;
		double _captureHeight = 99.0;
		double _captureDistance = 720.0;
		int32 _phase = 0;
		int32 _motion = 0;
		int32 _angle = 0;
		int32 _captureSide = 1;
		int32 _videoFrames = 0;
		double _started = 0.0;
		double _then = 0.0;
		double _lastSample = 0.0;
		double _nextVideo = 0.0;
		double _minSole = MAX_dbl;
		double _maxSole = -MAX_dbl;
		double _maxNeck = 0.0;
		double _maxBone = 0.0;
	};
}

void FRuntimeAppearanceTest::GetTests(TArray<FString>& names, TArray<FString>& commands) const
{
	if (FParse::Param(FCommandLine::Get(), TEXT("WardrobePreview")))
	{
		const TCHAR lastForm = FParse::Param(FCommandLine::Get(), TEXT("WardrobeAllCharacters")) ? TEXT('D') : TEXT('A');
		for (const TCHAR* species : { TEXT("Human"), TEXT("Elf"), TEXT("Orc"), TEXT("Dwarf") })
		{
			for (const TCHAR* sex : { TEXT("Male"), TEXT("Female") })
			{
				for (TCHAR form = TEXT('A'); form <= lastForm; form++)
				{
					const FString name = FString::Printf(TEXT("%s.%s%c"), species, sex, form);
					names.Add(name);
					commands.Add(name);
				}
			}
		}
		return;
	}

	for (const TCHAR* species : { TEXT("Orc"), TEXT("Dwarf") })
	{
		for (const TCHAR* sex : { TEXT("Male"), TEXT("Female") })
		{
			for (TCHAR form = TEXT('A'); form <= TEXT('D'); form++)
			{
				const FString name = FString::Printf(TEXT("%s.%s%c"), species, sex, form);
				names.Add(name);
				commands.Add(name);
			}
		}
	}
}

bool FRuntimeAppearanceTest::RunTest(const FString& parameters)
{
	if (parameters.StartsWith(TEXT("Human")) || parameters.StartsWith(TEXT("Elf")))
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("WardrobePreview")))
		{
			AddError(TEXT("Human and Elf wardrobe captures require -WardrobePreview."));
			return false;
		}
	}

	AddCommand(new FRuntimeAppearanceCommand(*this, parameters));
	return true;
}

#endif
