#if WITH_DEV_AUTOMATION_TESTS

#include "Appearance.h"
#include "Appearance/AppearanceChoices.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GroomComponent.h"
#include "GroomAsset.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UI/UISubsystem.h"
#include "UnrealClient.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMenuEquipmentTest, "Alverion.Characters.MenuEquipment", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** Checks equipment on the front end's own avatar and camera rather than a separate preview actor. */
	class FMenuEquipmentCommand : public IAutomationLatentCommand
	{
	public:
		FMenuEquipmentCommand(FAutomationTestBase& test, const FString& parameters) : _test(test), _parameters(parameters)
		{
		}

		virtual bool Update() override
		{
			const double now = FPlatformTime::Seconds();
			if (_started == 0.0)
			{
				_started = now;
			}

			if (now - _started > 60.0)
			{
				_test.AddError(TEXT("The actual menu equipment test timed out: ") + _parameters);
				return true;
			}

			if (_phase == 2)
			{
				if (now - _then <= 1.0)
				{
					return false;
				}

				if (_rearCamera.IsValid())
				{
					_rearCamera->Destroy();
				}
				return true;
			}

			UWorld* world = nullptr;
			for (const FWorldContext& context : GEngine->GetWorldContexts())
			{
				if (context.WorldType == EWorldType::Game)
				{
					world = context.World();
					break;
				}
			}

			if (world == nullptr || world->GetGameInstance() == nullptr)
			{
				return false;
			}

			UUISubsystem* ui = world->GetGameInstance()->GetSubsystem<UUISubsystem>();
			APlayerController* controller = world->GetFirstPlayerController();
			if (ui == nullptr || !ui->showAvatar || controller == nullptr)
			{
				return false;
			}

			const bool creation = _parameters.EndsWith(TEXT("Create"));
			if (_phase == 0)
			{
				Network::FAppearance look;
				look.male = _parameters.Contains(TEXT("Male")) && !_parameters.Contains(TEXT("Female"));
				int32 hairStyle = 0;
				if (FParse::Value(FCommandLine::Get(), TEXT("HeadwearHairStyle="), hairStyle))
				{
					look.hairStyle = static_cast<uint8>(FMath::Clamp(hairStyle, 0, static_cast<int32>(AppearanceChoices::baldHairStyle)));
				}
				_bald = look.hairStyle == AppearanceChoices::baldHairStyle;
				if (_parameters.Contains(TEXT("HeadB")))
				{
					look.head = 1;
				}
				else if (_parameters.Contains(TEXT("HeadC")))
				{
					look.head = 2;
				}
				else if (_parameters.Contains(TEXT("HeadD")))
				{
					look.head = 3;
				}

				if (_parameters.StartsWith(TEXT("Elf")))
				{
					look.species = Network::EAppearanceSpecies::Elf;
				}
				else if (_parameters.StartsWith(TEXT("Orc")))
				{
					look.species = Network::EAppearanceSpecies::Orc;
				}
				else if (_parameters.StartsWith(TEXT("Dwarf")))
				{
					look.species = Network::EAppearanceSpecies::Dwarf;
				}

				UCommonActivatableWidgetStack* menu = ui->getLayer(EUILayer::Menu);
				if (menu != nullptr)
				{
					menu->SetVisibility(ESlateVisibility::Collapsed);
				}

				const TArray<uint8> bytes = look.pack();
				// Selection shows the server's starting equipment, the villager clothes and the wooden sword, and creation none of it.
				const TArray<int32> starter = { 30001, 30002, 30003, 30004, 30005, 30006 };
				ui->showAvatar(creation ? EAvatarSpot::Create : EAvatarSpot::Select, bytes, creation ? TConstArrayView<int32>() : TConstArrayView<int32>(starter));
				const FName tag = creation ? FName(TEXT("FrontEndStop.Create")) : FName(TEXT("FrontEndStop.Select"));
				for (TActorIterator<ACameraActor> iterator(world); iterator; ++iterator)
				{
					if (iterator->ActorHasTag(tag))
					{
						controller->SetViewTarget(*iterator);
						break;
					}
				}

				_then = now;
				_phase = 1;
				return false;
			}

			if (now - _then < 5.0)
			{
				return false;
			}

			AActor* avatar = nullptr;
			for (TActorIterator<AActor> iterator(world); iterator; ++iterator)
			{
				if (FindObjectFast<USkeletalMeshComponent>(*iterator, TEXT("SKM_Villager_Braies")) != nullptr)
				{
					avatar = *iterator;
					break;
				}
			}

			if (avatar == nullptr)
			{
				return false;
			}

			USkeletalMeshComponent* body = FindObjectFast<USkeletalMeshComponent>(avatar, TEXT("Body"));
			USkeletalMeshComponent* gloves = FindObjectFast<USkeletalMeshComponent>(avatar, TEXT("SKM_Villager_Gloves"));
			if (!_test.TestNotNull(TEXT("The actual menu avatar has a body"), body) || !_test.TestNotNull(TEXT("The actual menu avatar has gloves"), gloves))
			{
				return true;
			}

			if (!_rearCamera.IsValid())
			{
				if (FParse::Param(FCommandLine::Get(), TEXT("HeadwearHairRearView")))
				{
					const FVector head = body->GetSocketLocation(TEXT("head"));
					const FVector direction = (controller->PlayerCameraManager->GetCameraLocation() - head).GetSafeNormal2D();
					const FVector position = head - direction * 100.0 + FVector::UpVector * 2.0;
					ACameraActor* camera = world->SpawnActor<ACameraActor>(position, (head - position).Rotation());
					camera->GetCameraComponent()->SetFieldOfView(40.0f);
					controller->SetViewTarget(camera);
					_rearCamera = camera;
					_then = now;
					return false;
				}
			}

			_test.TestEqual(TEXT("Creation hides gloves and selection shows them"), gloves->IsVisible(), !creation);
			USkeletalMeshComponent* hat = FindObjectFast<USkeletalMeshComponent>(avatar, TEXT("SKM_Villager_Hat"));
			if (_test.TestNotNull(TEXT("The actual menu avatar owns its starter hat"), hat))
			{
				_test.TestEqual(TEXT("Creation hides the hat and selection shows it"), hat->IsVisible(), !creation);
				_test.TestEqual(TEXT("The menu uses the accepted hat asset"), hat->GetSkeletalMeshAsset()->GetName(), TEXT("SKM_Villager_Hat_Cowboy"));
			}

			UGroomComponent* hair = FindObjectFast<UGroomComponent>(avatar, TEXT("Hair"));
			if (_test.TestNotNull(TEXT("The actual menu avatar owns hair"), hair))
			{
				_test.TestEqual(TEXT("The chosen hairstyle remains visible and the bald choice remains bald"), hair->IsVisible(), !_bald);
				_test.TestEqual(TEXT("Headwear uses clipped hair cards and creation restores normal hair"), hair->bUseCards, !creation);
				const TArray<float>& clipping = hair->GetCustomPrimitiveData().Data;
				if (_test.TestTrue(TEXT("Hair has a headwear clipping control"), clipping.Num() > 0))
				{
					_test.TestEqual(TEXT("The clipping control follows headwear visibility"), clipping[0], creation ? 0.0f : 1.0f);
				}

				if (!creation && !_bald)
				{
					const int32 materialCount = hair->GetNumMaterials();
					for (int32 index = 0; index < materialCount; index++)
					{
						UMaterialInterface* raw = hair->GetMaterial(index, EHairGeometryType::Cards, false);
						UMaterialInstanceDynamic* material = Cast<UMaterialInstanceDynamic>(raw);
						if (_test.TestNotNull(TEXT("Headwear retains a dynamic hair material for every detail level"), material))
						{
							_test.AddInfo(FString::Printf(TEXT("Hair slot %d: %s, geometry %d, masked %d, enabled %.2f, plane %.3f %.3f %.3f %.3f."), index, *material->Parent->GetPathName(), static_cast<int32>(hair->GetMaterialGeometryType(index)), material->GetBlendMode() == BLEND_Masked, material->K2_GetScalarParameterValue(TEXT("HeadwearEnabled")), material->K2_GetScalarParameterValue(TEXT("HeadwearNx")), material->K2_GetScalarParameterValue(TEXT("HeadwearNy")), material->K2_GetScalarParameterValue(TEXT("HeadwearNz")), material->K2_GetScalarParameterValue(TEXT("HeadwearDistance"))));
							_test.TestEqual(TEXT("Every hair material receives the enabled headwear mask"), material->K2_GetScalarParameterValue(TEXT("HeadwearEnabled")), 1.0f);
							_test.TestEqual(TEXT("Every hair material supports the headwear opacity mask"), material->GetBlendMode(), BLEND_Masked);
						}
					}
				}
			}

			_test.AddInfo(FString::Printf(TEXT("Actual menu body LOD %d, glove LOD %d, glove bounds %s."), body->GetPredictedLODLevel(), gloves->GetPredictedLODLevel(), *gloves->Bounds.BoxExtent.ToString()));

			if (!creation)
			{
				const FSkeletalMeshRenderData* renderData = gloves->GetSkeletalMeshRenderData();
				TArray<FMatrix44f> matrices;
				TArray<FVector3f> positions;
				gloves->GetCurrentRefToLocalMatrices(matrices, 0);
				USkinnedMeshComponent::ComputeSkinnedPositions(gloves, positions, matrices, renderData->LODRenderData[0], *gloves->GetSkinWeightBuffer(0));
				const FVector left = body->GetSocketLocation(TEXT("hand_l"));
				const FVector right = body->GetSocketLocation(TEXT("hand_r"));
				double maximum = 0.0;
				for (const FVector3f& position : positions)
				{
					const FVector point = gloves->GetComponentTransform().TransformPosition(FVector(position));
					maximum = FMath::Max(maximum, FMath::Min(FVector::Distance(point, left), FVector::Distance(point, right)));
				}

				_test.TestTrue(TEXT("The visible menu gloves contain vertices"), positions.Num() > 0);
				_test.AddInfo(FString::Printf(TEXT("Actual menu glove maximum hand distance %.3f cm."), maximum));
				_test.TestTrue(TEXT("The rendered menu glove surface stays within 35 cm of a hand"), maximum < 35.0);
			}
			FString folder;
			if (FParse::Value(FCommandLine::Get(), TEXT("MenuEquipmentCapture="), folder))
			{
				FScreenshotRequest::RequestScreenshot(folder / (_parameters + TEXT(".png")), false, false);
			}

			_phase = 2;
			_then = now;
			return false;
		}

	private:
		FAutomationTestBase& _test;
		FString _parameters;
		double _started = 0.0;
		double _then = 0.0;
		int32 _phase = 0;
		bool _bald = false;
		TWeakObjectPtr<ACameraActor> _rearCamera;
	};
}

void FMenuEquipmentTest::GetTests(TArray<FString>& names, TArray<FString>& commands) const
{
	for (const TCHAR* race : { TEXT("Human"), TEXT("Elf"), TEXT("Orc"), TEXT("Dwarf") })
	{
		for (const TCHAR* sex : { TEXT("Male"), TEXT("Female") })
		{
			for (const TCHAR* screen : { TEXT("Select"), TEXT("Create") })
			{
				if (FString(screen) == TEXT("Create"))
				{
					if (FParse::Param(FCommandLine::Get(), TEXT("MenuEquipmentSelectionOnly")))
					{
						continue;
					}
				}

				const FString name = FString(race) + TEXT(".") + sex + TEXT(".") + screen;
				names.Add(name);
				commands.Add(name);
				if (FString(screen) == TEXT("Select"))
				{
					if (FParse::Param(FCommandLine::Get(), TEXT("MenuEquipmentAllHeads")))
					{
						for (const TCHAR head : { TEXT('B'), TEXT('C'), TEXT('D') })
						{
							const FString variant = FString(race) + TEXT(".") + sex + TEXT(".Head") + FString::Chr(head) + TEXT(".Select");
							names.Add(variant);
							commands.Add(variant);
						}
					}
				}
			}
		}
	}
}

bool FMenuEquipmentTest::RunTest(const FString& parameters)
{
	AddCommand(new FMenuEquipmentCommand(*this, parameters));
	return true;
}

#endif
