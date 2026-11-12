#include "Appearance/HeadwearHairComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GroomComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const FName enabledParameter(TEXT("HeadwearEnabled"));
	const FName normalXParameter(TEXT("HeadwearNx"));
	const FName normalYParameter(TEXT("HeadwearNy"));
	const FName normalZParameter(TEXT("HeadwearNz"));
	const FName distanceParameter(TEXT("HeadwearDistance"));
}

UHeadwearHairComponent::UHeadwearHairComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UHeadwearHairComponent::configure(UGroomComponent* hair, USkeletalMeshComponent* body, UMeshComponent* headwear, float openingOffsetCm)
{
	if (_body != body)
	{
		if (_body != nullptr)
		{
			RemoveTickPrerequisiteComponent(_body);
		}

		if (body != nullptr)
		{
			AddTickPrerequisiteComponent(body);
		}
	}

	_hair = hair;
	_body = body;
	_headwear = headwear;
	SetComponentTickEnabled(false);
	if (hair == nullptr)
	{
		return;
	}

	hair->SetCustomPrimitiveDataFloat(0, 0.0f);
	const int32 materialCount = hair->GetNumMaterials();
	for (int32 index = 0; index < materialCount; index++)
	{
		UMaterialInstanceDynamic* material = Cast<UMaterialInstanceDynamic>(hair->GetMaterial(index));
		if (material != nullptr)
		{
			material->SetScalarParameterValue(enabledParameter, 0.0f);
		}
	}
	if (body == nullptr || headwear == nullptr || !body->DoesSocketExist(TEXT("head")))
	{
		return;
	}

	const USkeletalMeshComponent* skeletalHeadwear = Cast<USkeletalMeshComponent>(headwear);
	if (skeletalHeadwear != nullptr)
	{
		USkeletalMesh* mesh = skeletalHeadwear->GetSkeletalMeshAsset();
		USkeletalMesh* bodyMesh = body->GetSkeletalMeshAsset();
		if (mesh == nullptr || bodyMesh == nullptr)
		{
			return;
		}

		const FBox bounds = mesh->GetImportedBounds().GetBox();
		const FVector opening(bounds.GetCenter().X, bounds.GetCenter().Y, bounds.Min.Z + openingOffsetCm);
		const FTransform referenceHead(bodyMesh->GetComposedRefPoseMatrix(FName(TEXT("head"))));
		const FTransform pieceToBody = headwear->GetComponentTransform().GetRelativeTransform(body->GetComponentTransform());
		_openingPoint = referenceHead.InverseTransformPosition(pieceToBody.TransformPosition(opening));
		_openingNormal = referenceHead.InverseTransformVectorNoScale(pieceToBody.TransformVectorNoScale(FVector::UpVector)).GetSafeNormal();
	}
	else
	{
		const UStaticMeshComponent* staticHeadwear = Cast<UStaticMeshComponent>(headwear);
		if (staticHeadwear == nullptr || staticHeadwear->GetStaticMesh() == nullptr)
		{
			return;
		}

		const FBox bounds = staticHeadwear->GetStaticMesh()->GetBoundingBox();
		const FVector opening(bounds.GetCenter().X, bounds.GetCenter().Y, bounds.Min.Z + openingOffsetCm);
		const FTransform head = body->GetSocketTransform(TEXT("head"));
		_openingPoint = head.InverseTransformPosition(headwear->GetComponentTransform().TransformPosition(opening));
		_openingNormal = head.InverseTransformVectorNoScale(headwear->GetUpVector()).GetSafeNormal();
	}

	SetComponentTickEnabled(true);
	TickComponent(0.0f, LEVELTICK_All, nullptr);
}

void UHeadwearHairComponent::TickComponent(float deltaTime, ELevelTick tickType, FActorComponentTickFunction* tickFunction)
{
	Super::TickComponent(deltaTime, tickType, tickFunction);
	if (_hair == nullptr || _body == nullptr || _headwear == nullptr)
	{
		return;
	}

	const FTransform head = _body->GetSocketTransform(TEXT("head"));
	const FVector normal = head.TransformVectorNoScale(_openingNormal).GetSafeNormal();
	const FVector point = head.TransformPosition(_openingPoint);
	const float distance = static_cast<float>(FVector::DotProduct(normal, point));
	const float enabled = _headwear->IsVisible() ? 1.0f : 0.0f;
	_hair->SetCustomPrimitiveDataVector4(1, FVector4(normal.X, normal.Y, normal.Z, distance));
	_hair->SetCustomPrimitiveDataFloat(0, enabled);
	const int32 materialCount = _hair->GetNumMaterials();
	for (int32 index = 0; index < materialCount; index++)
	{
		UMaterialInstanceDynamic* material = Cast<UMaterialInstanceDynamic>(_hair->GetMaterial(index));
		if (material != nullptr)
		{
			material->SetScalarParameterValue(enabledParameter, enabled);
			material->SetScalarParameterValue(normalXParameter, static_cast<float>(normal.X));
			material->SetScalarParameterValue(normalYParameter, static_cast<float>(normal.Y));
			material->SetScalarParameterValue(normalZParameter, static_cast<float>(normal.Z));
			material->SetScalarParameterValue(distanceParameter, distance);
		}
	}
}
