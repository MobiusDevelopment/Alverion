#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "DamageNumbersComponent.generated.h"

class UTextRenderComponent;

/** Numbers that rise over a creature as it is struck and fade away: the damage, larger and gold for a critical, or the word for a miss. */
UCLASS()
class ALVERION_API UDamageNumbersComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDamageNumbersComponent();

	/** Shows one blow, with the hit flags of the server's Attack. */
	void show(int32 damage, uint8 flags);

	virtual void TickComponent(float deltaSeconds, ELevelTick tickType, FActorComponentTickFunction* thisTickFunction) override;

private:
	/** Each number on show and how long it has been up. The owner keeps the components, so a weak pointer does here. */
	TArray<TPair<TWeakObjectPtr<UTextRenderComponent>, float>> _floating;
};
