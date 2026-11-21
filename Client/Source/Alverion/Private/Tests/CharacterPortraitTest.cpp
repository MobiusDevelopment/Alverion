#if WITH_DEV_AUTOMATION_TESTS

#include "Appearance/AppearanceApplier.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "RHI.h"
#include "Slate/WidgetRenderer.h"
#include "UI/InventoryWidget.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"
#include "World/WorldCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterPortraitTest, "Alverion.UI.CharacterPortrait", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCharacterPortraitTest::RunTest(const FString& parameters)
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

	AWorldCharacter* character = world->SpawnActor<AWorldCharacter>();
	UTextureRenderTarget2D* target = character->beginPortrait();
	if (!TestNotNull(TEXT("The character sheet has a portrait target"), target))
	{
		return false;
	}

	const USceneCaptureComponent2D* capture = nullptr;
	for (TObjectIterator<USceneCaptureComponent2D> component; component; ++component)
	{
		if (component->TextureTarget == target)
		{
			capture = *component;
			break;
		}
	}

	if (TestNotNull(TEXT("The portrait target has a camera"), capture))
	{
		TestTrue(TEXT("The camera lives in a separate world"), capture->GetWorld() != world);
		TestEqual(TEXT("The portrait capture carries inverse opacity"), capture->CaptureSource.GetValue(), SCS_SceneColorHDR);
	}

	ON_SCOPE_EXIT
	{
		character->Destroy();
	};
	if (GUsingNullRHI || (capture == nullptr))
	{
		character->endPortrait();
		return true;
	}

	character->tickPortrait(1.0f / 60.0f);
	TArray<FLinearColor> pixels;
	FTextureRenderTargetResource* resource = target->GameThread_GetRenderTargetResource();
	if (!TestTrue(TEXT("The isolated scene renders pixels"), resource->ReadLinearColorPixels(pixels)) || pixels.IsEmpty())
	{
		return false;
	}

	const int32 middle = (target->SizeY / 2) * target->SizeX + (target->SizeX / 2);
	TestTrue(TEXT("The corner has zero opacity"), pixels[0].A > 0.99f);
	TestTrue(TEXT("The model is opaque"), pixels[middle].A < 0.01f);
	TestTrue(TEXT("The studio lights illuminate the model"), pixels[middle].GetLuminance() > 0.05f);

	UCharacterPortraitImage* image = NewObject<UCharacterPortraitImage>();
	image->SetBrushResourceObject(target);
	const TSharedRef<SWidget> widget = image->TakeWidget();
	FWidgetRenderer renderer(true);
	UTextureRenderTarget2D* composite = renderer.DrawWidget(widget, FVector2D(target->SizeX, target->SizeY));
	if (TestNotNull(TEXT("Slate composites the portrait image"), composite))
	{
		TArray<FColor> composited;
		if (TestTrue(TEXT("The sheet image renders pixels"), composite->GameThread_GetRenderTargetResource()->ReadPixels(composited)) && !composited.IsEmpty())
		{
			TestEqual(TEXT("The sheet image leaves the background transparent"), composited[0].A, uint8(0));
			TestEqual(TEXT("The sheet image keeps the model opaque"), composited[middle].A, uint8(255));

			image->SetRenderOpacity(0.5f);
			renderer.DrawWidget(composite, widget, FVector2D(target->SizeX, target->SizeY), 0.0f);
			if (TestTrue(TEXT("The faded sheet image renders pixels"), composite->GameThread_GetRenderTargetResource()->ReadPixels(composited)) && !composited.IsEmpty())
			{
				TestEqual(TEXT("Fading the model keeps the background transparent"), composited[0].A, uint8(0));
				TestTrue(TEXT("The model fades with the sheet"), (composited[middle].A > 0) && (composited[middle].A < 255));
			}
		}
	}

	UWorld* studio = capture->GetWorld();
	if (FPackageName::DoesPackageExist(TEXT("/Game/Characters/MetaHumans/Humans/Male/Victor/BP_MH_Victor")))
	{
		if (!TestNotNull(TEXT("The requested character model loads"), LoadClass<AActor>(nullptr, TEXT("/Game/Characters/MetaHumans/Humans/Male/Victor/BP_MH_Victor.BP_MH_Victor_C"))))
		{
			return false;
		}

		Network::FAppearance appearance;
		appearance.male = true;
		appearance.head = 0;
		character->wear(appearance);
		world->Tick(LEVELTICK_All, 1.0f / 60.0f);
		TArray<AActor*> wornBodies;
		character->GetAttachedActors(wornBodies);
		if (TestEqual(TEXT("The gameplay character wears a body"), wornBodies.Num(), 1))
		{
			wornBodies[0]->SetActorHiddenInGame(true);
			character->tickPortrait(1.0f / 60.0f);
			if (resource->ReadLinearColorPixels(pixels) && pixels.IsValidIndex(middle))
			{
				TestTrue(TEXT("The equipped character renders in the portrait"), pixels[middle].A < 0.01f);
			}

			AActor* previewBody = nullptr;
			for (TActorIterator<AActor> actor(studio, wornBodies[0]->GetClass()); actor; ++actor)
			{
				previewBody = *actor;
				break;
			}

			if (TestNotNull(TEXT("The studio has the same character model"), previewBody))
			{
				TestFalse(TEXT("The portrait stays visible when first person hides the gameplay body"), previewBody->IsHidden());
				TestEqual(TEXT("The portrait presents the same sword equipment"), Appearance::wieldsSword(*previewBody), Appearance::wieldsSword(*wornBodies[0]));
			}
		}
	}

	const double studioTime = studio->GetTimeSeconds();
	character->endPortrait();
	character->tickPortrait(1.0f / 60.0f);
	TestEqual(TEXT("The studio stops ticking when the sheet closes"), studio->GetTimeSeconds(), studioTime);
	return true;
}

#endif
