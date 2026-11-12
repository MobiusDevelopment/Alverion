#if WITH_DEV_AUTOMATION_TESTS

#include "Appearance.h"
#include "Appearance/AppearanceApplier.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "World/ProxyCharacter.h"
#include "World/WorldCharacter.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FOrcWorldScaleTest, "Alverion.Characters.OrcWorldScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FOrcWorldScaleTest::GetTests(TArray<FString>& names, TArray<FString>& commands) const
{
	for (const TCHAR* host : { TEXT("Local"), TEXT("Proxy") })
	{
		for (const TCHAR* sex : { TEXT("Female"), TEXT("Male") })
		{
			for (TCHAR form = TEXT('A'); form <= TEXT('D'); ++form)
			{
				const FString name = FString::Printf(TEXT("%s.%s%c"), host, sex, form);
				names.Add(name);
				commands.Add(name);
			}
		}
	}
}

bool FOrcWorldScaleTest::RunTest(const FString& parameters)
{
	const TCHAR form = parameters[parameters.Len() - 1];
	Network::FAppearance look;
	look.species = Network::EAppearanceSpecies::Orc;
	look.male = parameters.Contains(TEXT(".Male"));
	look.head = static_cast<uint8>(form - TEXT('A'));
	const FString suffix = (look.head == 0) ? FString() : FString::Chr(form);
	const TCHAR* const donors[2][4] = { { TEXT("Jelena"), TEXT("Vivian"), TEXT("Aera"), TEXT("Ada") }, { TEXT("Victor"), TEXT("Orlando"), TEXT("Kelvin"), TEXT("Cameron") } };
	const FString name = FString(look.male ? TEXT("MH_OrcMale") : TEXT("MH_OrcFemale")) + suffix;
	const FString path = FString::Printf(TEXT("/Game/Characters/MetaHumans/Orcs/%s/%s/BP_%s.BP_%s_C"), look.male ? TEXT("Male") : TEXT("Female"), donors[look.male ? 1 : 0][look.head], *name, *name);
	UClass* expected = LoadClass<AActor>(nullptr, *path);
	if (!TestNotNull(*name, expected))
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
	AActor* host = nullptr;
	FVector feet = FVector::ZeroVector;
	if (parameters.StartsWith(TEXT("Local.")))
	{
		AWorldCharacter* player = world->SpawnActor<AWorldCharacter>();
		player->SetActorLocation(FVector(100.0, 200.0, 300.0));
		feet.Z = -player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		player->wear(look);
		host = player;
	}
	else
	{
		AProxyCharacter* proxy = world->SpawnActor<AProxyCharacter>();
		Network::FCharacterSpawn spawn;
		spawn.location = FVector(100.0, 200.0, 300.0);
		spawn.appearance = look.pack();
		proxy->setup(spawn);
		host = proxy;
	}

	world->Tick(LEVELTICK_All, 1.0f / 60.0f);
	TArray<AActor*> bodies;
	host->GetAttachedActors(bodies);
	if (!TestEqual(TEXT("The world attaches one visual body"), bodies.Num(), 1))
	{
		return false;
	}

	AActor* visual = bodies[0];
	TestEqual(TEXT("The world wears the selected orc"), visual->GetClass(), expected);
	TestTrue(TEXT("The actor attaches to the host's foot origin"), visual->GetRootComponent()->GetRelativeLocation().Equals(feet, 0.01));
	TestTrue(TEXT("World attachment keeps the uniform orc scale"), visual->GetActorScale3D().Equals(FVector(1.08), 0.001));
	TestTrue(TEXT("The host keeps its original gameplay scale"), host->GetActorScale3D().Equals(FVector::OneVector, 0.001));
	USkeletalMeshComponent* body = FindObjectFast<USkeletalMeshComponent>(visual, TEXT("Body"));
	USkeletalMeshComponent* face = FindObjectFast<USkeletalMeshComponent>(visual, TEXT("Face"));
	if (!TestNotNull(TEXT("The world orc has a body"), body) || !TestNotNull(TEXT("The world orc has a face"), face))
	{
		return false;
	}

	TestTrue(TEXT("The body inherits the world orc scale"), body->GetComponentScale().Equals(FVector(1.08), 0.001));
	TestTrue(TEXT("The face inherits the same scale once"), face->GetComponentScale().Equals(FVector(1.08), 0.001));

	// The body grows around a head that keeps its size, and stays on its feet.
	body->TickAnimation(0.0f, false);
	body->RefreshBoneTransforms();
	face->TickAnimation(0.0f, false);
	face->RefreshBoneTransforms();
	const int32 headBone = face->GetBoneIndex(TEXT("head"));
	const FVector approvedHeadScale(0.95);
	TestTrue(TEXT("The orc head keeps the balanced scale without resizing the body"), (headBone != INDEX_NONE) && face->GetComponentSpaceTransforms()[headBone].GetScale3D().Equals(approvedHeadScale, 0.001));
	TestTrue(TEXT("The world orc head scale exceeds the unit human head scale"), face->GetSocketTransform(TEXT("head"), RTS_World).GetScale3D().GetMin() > 1.0);
	face->SetDisablePostProcessBlueprint(false);
	Appearance::apply(*visual, look);
	TestTrue(TEXT("Refreshing a cached orc restores the approved photo face state"), face->GetDisablePostProcessBlueprint());
	TestTrue(TEXT("The face follows the grown neck"), body->GetSocketLocation(TEXT("head")).Equals(face->GetSocketLocation(TEXT("head")), 0.1));
	TestEqual(TEXT("Runtime height matches the approved actor scale without extra growth"), Appearance::heightScale(*visual), 1.08f);
	for (const FName bone : { FName(TEXT("root")), FName(TEXT("pelvis")) })
	{
		const int32 index = body->GetBoneIndex(bone);
		if (TestTrue(TEXT("The runtime body has its shape bone"), index != INDEX_NONE))
		{
			TestTrue(*FString::Printf(TEXT("%s retains approved unit bone scale"), *bone.ToString()), body->GetComponentSpaceTransforms()[index].GetScale3D().Equals(FVector::OneVector, 0.001));
		}
	}

	TArray<FMatrix44f> matrices;
	TArray<FVector3f> positions;
	body->GetCurrentRefToLocalMatrices(matrices, 0);
	USkinnedMeshComponent::ComputeSkinnedPositions(body, positions, matrices, body->GetSkeletalMeshRenderData()->LODRenderData[0], *body->GetSkinWeightBuffer(0));
	double sole = MAX_dbl;
	for (const FVector3f& position : positions)
	{
		sole = FMath::Min(sole, body->GetComponentTransform().TransformPosition(FVector(position)).Z - visual->GetActorLocation().Z);
	}

	AddInfo(FString::Printf(TEXT("%s stands with its sole %.2f cm off the ground."), *name, sole));
	TestTrue(TEXT("The grown orc stands on the ground"), FMath::Abs(sole) < 1.5);
	if (host->IsA<AWorldCharacter>())
	{
		const USpringArmComponent* arm = host->FindComponentByClass<USpringArmComponent>();
		if (!TestNotNull(TEXT("The local orc has a camera arm"), arm))
		{
			return false;
		}

		TestEqual(TEXT("The third-person camera follows the approved actor height"), arm->GetRelativeLocation().Z, 150.0 * 1.08 - 90.0, 0.01);
	}

	return true;
}

#endif
