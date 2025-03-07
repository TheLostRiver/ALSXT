#pragma once

#include "ALSXTCombatState.generated.h"

USTRUCT(BlueprintType)
struct ALSXT_API FALSXTCombatState
{
	GENERATED_BODY()

		UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ClampMin = -180, ClampMax = 180, ForceUnits = "deg"))
		float TargetYawAngle{ 0.0f };
};

