// MIT

#include "Components/Character/ALSXTCombatComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Utility/AlsUtility.h"
#include "Utility/AlsMacros.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Utility/ALSXTEnums.h"
#include "Settings/ALSXTCharacterSettings.h"
#include "Math/Vector.h"
#include "GameFramework/Character.h"
#include "ALSXTCharacter.h"
#include "Interfaces/ALSXTTargetLockInterface.h"
#include "Interfaces/ALSXTCombatInterface.h"

// Sets default values for this component's properties
UALSXTCombatComponent::UALSXTCombatComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);

	// ...
}


// Called when the game starts
void UALSXTCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	Character = Cast<AALSXTCharacter>(GetOwner());
	AlsCharacter = Cast<AAlsCharacter>(GetOwner());

	auto* EnhancedInput{ Cast<UEnhancedInputComponent>(Character) };
	if (IsValid(EnhancedInput))
	{
		//FSetupPlayerInputComponentDelegate Del = Character->OnSetupPlayerInputComponentUpdated;
		//Del.AddUniqueDynamic(this, &UALSXTCombatComponent::SetupInputComponent(EnhancedInput));
	}
	TargetTraceTimerDelegate.BindUFunction(this, "TryTraceForTargets");
}


// Called every frame
void UALSXTCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshAttack(DeltaTime);
}

float UALSXTCombatComponent::GetAngle(FVector Target)
{
	float resultAngleInRadians = 0.0f;
	FVector PlayerLocation = Character->GetActorLocation();
	PlayerLocation.Normalize();
	Target.Normalize();

	auto crossProduct = PlayerLocation.Cross(Target);
	auto dotProduct = PlayerLocation.Dot(Target);

	if (crossProduct.Z > 0)
	{
		resultAngleInRadians = acosf(dotProduct);
	}
	else
	{
		resultAngleInRadians = -1 * acosf(dotProduct);
	}

	auto resultAngleInDegrees = FMath::RadiansToDegrees(resultAngleInRadians);
	return resultAngleInDegrees;
}

bool UALSXTCombatComponent::IsTartgetObstructed()
{
	FVector CharLoc = Character->GetActorLocation();
	TArray<FHitResult> OutHits;

	FCollisionObjectQueryParams ObjectQueryParameters;
	for (const auto ObjectType : CombatSettings.ObstructionTraceObjectTypes)
	{
		ObjectQueryParameters.AddObjectTypesToQuery(UCollisionProfile::Get()->ConvertToCollisionChannel(false, ObjectType));
	}

	if (GetWorld()->LineTraceMultiByObjectType(OutHits, CharLoc, CurrentTarget.HitResult.GetActor()->GetActorLocation(), ObjectQueryParameters))
	{
		if (CombatSettings.UnlockWhenTargetIsObstructed)
		{
			ClearCurrentTarget();
		}
		return true;
	}
	else
	{
		return false;
	}
}

void UALSXTCombatComponent::TryTraceForTargets()
{
	TArray<FGameplayTag> TargetableOverlayModes;
	GetTargetableOverlayModes(TargetableOverlayModes);

	if (Character && TargetableOverlayModes.Contains(Character->GetOverlayMode()) && Character->IsDesiredAiming() && IsValid(CurrentTarget.HitResult.GetActor()))
	{
		if (Character->GetDistanceTo(CurrentTarget.HitResult.GetActor()) < CombatSettings.MaxInitialLockDistance)
		{			
			if (CombatSettings.UnlockWhenTargetIsObstructed) {
				if (CombatSettings.UnlockWhenTargetIsObstructed && !IsTartgetObstructed()) {
					if (CurrentTarget.Valid)
					{
						RotatePlayerToTarget(CurrentTarget);
					}
				}
				else
				{
					DisengageAllTargets();
					OnTargetObstructed();
				}
			}
			else
			{
				if (CurrentTarget.Valid)
				{
					RotatePlayerToTarget(CurrentTarget);
				}
			}			
		}
		else
		{
			DisengageAllTargets();
		}
	}
}

void UALSXTCombatComponent::TraceForTargets(TArray<FTargetHitResultEntry>& Targets)
{
	FRotator ControlRotation = Character->GetControlRotation();
	FVector CharLoc = Character->GetActorLocation();
	FVector ForwardVector = Character->GetActorForwardVector();
	FVector CameraLocation = Character->Camera->GetFirstPersonCameraLocation();
	FVector StartLocation = ForwardVector * 150 + CameraLocation;
	FVector EndLocation = ForwardVector * 200 + StartLocation;
	// FVector CenterLocation = (StartLocation - EndLocation) / 2 + StartLocation;
	FVector CenterLocation = (StartLocation - EndLocation) / 8 + StartLocation;
	// FVector CenterLocation = (StartLocation - EndLocation) / 8;
	FCollisionShape CollisionShape = FCollisionShape::MakeBox(CombatSettings.TraceAreaHalfSize);
	TArray<FHitResult> OutHits;

	// Display Debug Shape
	if (CombatSettings.DebugMode)
	{
		DrawDebugBox(GetWorld(), CenterLocation, CombatSettings.TraceAreaHalfSize, ControlRotation.Quaternion(), FColor::Yellow, false, CombatSettings.DebugDuration, 100, 2);
	}

	bool isHit = GetWorld()->SweepMultiByChannel(OutHits, StartLocation, EndLocation, ControlRotation.Quaternion(), ECollisionChannel::ECC_Camera, CollisionShape);

	if (isHit)
	{
		for (auto& Hit : OutHits)
		{
			if (Hit.GetActor()->GetClass()->ImplementsInterface(UALSXTTargetLockInterface::StaticClass()) && Hit.GetActor() != Character && Character->GetDistanceTo(Hit.GetActor()) < CombatSettings.MaxLockDistance)
			{
				FTargetHitResultEntry HitResultEntry;
				HitResultEntry.Valid = true;
				HitResultEntry.DistanceFromPlayer = FVector::Distance(Character->GetActorLocation(), Hit.Location);
				HitResultEntry.AngleFromCenter = GetAngle(Hit.Location);
				HitResultEntry.HitResult = Hit;
				Targets.Add(HitResultEntry);
			}
		}
	}
}

void UALSXTCombatComponent::GetClosestTarget()
{
	TArray<FTargetHitResultEntry> OutHits;
	TraceForTargets(OutHits);
	FTargetHitResultEntry FoundHit;
	TArray<FGameplayTag> TargetableOverlayModes;
	GetTargetableOverlayModes(TargetableOverlayModes);

	if (TargetableOverlayModes.Contains(Character->GetOverlayMode()) && Character->IsDesiredAiming())
	{
		for (auto& Hit : OutHits)
		{
			if (Hit.HitResult.GetActor() != Character)
			{
				FTargetHitResultEntry HitResultEntry;
				HitResultEntry.Valid = true;
				HitResultEntry.DistanceFromPlayer = Hit.DistanceFromPlayer;
				HitResultEntry.AngleFromCenter = Hit.AngleFromCenter;
				HitResultEntry.HitResult = Hit.HitResult;
				if (Hit.HitResult.GetActor() != CurrentTarget.HitResult.GetActor() && (HitResultEntry.DistanceFromPlayer < CurrentTarget.DistanceFromPlayer))
				{
					if (!FoundHit.Valid)
					{
						FoundHit = Hit;
					}
					else
					{
						if (Hit.AngleFromCenter < FoundHit.AngleFromCenter)
						{
							FoundHit = Hit;
						}
					}
					GetWorld()->GetTimerManager().SetTimer(TargetTraceTimerHandle, TargetTraceTimerDelegate, 0.001f, true);
				}
			}
		}
		if (FoundHit.Valid && FoundHit.HitResult.GetActor())
		{
			SetCurrentTarget(FoundHit);
			if (GEngine && CombatSettings.DebugMode)
			{
				FString DebugMsg = FString::SanitizeFloat(FoundHit.AngleFromCenter);
				DebugMsg.Append(" Hit Result: ");
				DebugMsg.Append(FoundHit.HitResult.GetActor()->GetName());
				GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, DebugMsg);
			}
		}
	}
}

void UALSXTCombatComponent::SetCurrentTarget(const FTargetHitResultEntry& NewTarget)
{
	ClearCurrentTarget();
	CurrentTarget = NewTarget;
	USkeletalMeshComponent* HitMesh;
	AALSXTCharacter* ALSXTChar = Cast<AALSXTCharacter>(CurrentTarget.HitResult.GetActor());

	if (ALSXTChar)
	{
		HitMesh = ALSXTChar->GetMesh();
		TArray<UMaterialInterface*> CharMaterials = HitMesh->GetMaterials();

		if (CharMaterials[0])
		{
			for (int m = 0; m < CharMaterials.Num(); m++)
			{
				UMaterialInstanceDynamic* CharDynMaterial = HitMesh->CreateAndSetMaterialInstanceDynamic(m);
				CharDynMaterial->SetScalarParameterValue(CombatSettings.HighlightMaterialParameterName, 1.0f);
				HitMesh->SetMaterial(m, CharDynMaterial);
				TargetDynamicMaterials.Add(CharDynMaterial);
			}
		}
	}	
}

void UALSXTCombatComponent::ClearCurrentTarget()
{
	if (CurrentTarget.Valid = true && CurrentTarget.HitResult.GetActor())
	{
		CurrentTarget.Valid = false;
		CurrentTarget.DistanceFromPlayer = 340282346638528859811704183484516925440.0f;
		CurrentTarget.AngleFromCenter = 361.0f;

		if (Cast<AALSXTCharacter>(CurrentTarget.HitResult.GetActor()))
		{
			USkeletalMeshComponent* HitMesh = Cast<AALSXTCharacter>(CurrentTarget.HitResult.GetActor())->GetMesh();
			TArray<UMaterialInterface*> CharMaterials = HitMesh->GetMaterials();
			if (TargetDynamicMaterials[0])
			{
				for (int m = 0; m < TargetDynamicMaterials.Num(); m++)
				{
					TargetDynamicMaterials[m]->SetScalarParameterValue(CombatSettings.HighlightMaterialParameterName, 0.0f);
					// HitMesh->SetMaterial(m, CharDynMaterial);
				}
				TargetDynamicMaterials.Empty();
			}
			// if (CharMaterials[0])
			// {
			// 	for (int m = 0; m < CharMaterials.Num(); m++)
			// 	{
			// 		UMaterialInstanceDynamic* CharDynMaterial = HitMesh->CreateAndSetMaterialInstanceDynamic(m);
			// 		CharDynMaterial->SetScalarParameterValue(CombatSettings.HighlightMaterialParameterName, 0.0f);
			// 		HitMesh->SetMaterial(m, CharDynMaterial);
			// 	}
			// }
		}
		CurrentTarget.HitResult = FHitResult(ForceInit);
	}
}

void UALSXTCombatComponent::DisengageAllTargets()
{
	ClearCurrentTarget();

	// Clear Trace Timer
	GetWorld()->GetTimerManager().ClearTimer(TargetTraceTimerHandle);
}

void UALSXTCombatComponent::GetTargetLeft()
{
	TArray<FTargetHitResultEntry> OutHits;
	TraceForTargets(OutHits);
	FTargetHitResultEntry FoundHit;
	TArray<FGameplayTag> TargetableOverlayModes;
	GetTargetableOverlayModes(TargetableOverlayModes);

	if (TargetableOverlayModes.Contains(Character->GetOverlayMode()) && Character->IsDesiredAiming())
	{
		for (auto& Hit : OutHits)
		{
			if (Hit.HitResult.GetActor() != CurrentTarget.HitResult.GetActor() && (Hit.AngleFromCenter < CurrentTarget.AngleFromCenter))
			{
				if (!FoundHit.Valid)
				{
					FoundHit = Hit;
				}
				else
				{
					if (Hit.AngleFromCenter > FoundHit.AngleFromCenter)
					{
						FoundHit = Hit;
					}
				}
			}
		}
		SetCurrentTarget(FoundHit);
	}
}

void UALSXTCombatComponent::GetTargetRight()
{
	TArray<FTargetHitResultEntry> OutHits;
	TraceForTargets(OutHits);
	FTargetHitResultEntry FoundHit;
	TArray<FGameplayTag> TargetableOverlayModes;
	GetTargetableOverlayModes(TargetableOverlayModes);

	if (TargetableOverlayModes.Contains(Character->GetOverlayMode()) && Character->IsDesiredAiming())
	{
		for (auto& Hit : OutHits)
		{
			if (Hit.HitResult.GetActor() != CurrentTarget.HitResult.GetActor() && (Hit.AngleFromCenter > CurrentTarget.AngleFromCenter))
			{
				if (!FoundHit.Valid)
				{
					FoundHit = Hit;
				}
				else
				{
					if (Hit.AngleFromCenter < FoundHit.AngleFromCenter)
					{
						FoundHit = Hit;
					}
				}
			}
		}
		SetCurrentTarget(FoundHit);
	}
}

void UALSXTCombatComponent::RotatePlayerToTarget(FTargetHitResultEntry Target)
{
	if (IsValid(Character) && IsValid(Character->GetController()) && Target.Valid && IsValid(Target.HitResult.GetActor()))
	{
		FRotator CurrentPlayerRotation = Character->GetActorRotation();
		FRotator CurrentPlayerControlRotation = Character->GetController()->GetControlRotation();
		FVector TargetActorLocation = Target.HitResult.GetActor()->GetActorLocation();
		FRotator NewPlayerRotation = UKismetMathLibrary::FindLookAtRotation(Character->GetActorLocation(), TargetActorLocation);
		NewPlayerRotation.Pitch = CurrentPlayerRotation.Pitch;
		NewPlayerRotation.Roll = CurrentPlayerRotation.Roll;

		if ((NewPlayerRotation != CurrentPlayerControlRotation) && IsValid(Target.HitResult.GetActor()))
		{
			// Character->SetActorRotation(NewPlayerRotation, ETeleportType::TeleportPhysics);
			Character->GetController()->SetControlRotation(NewPlayerRotation);
		}
	}
}

// Attack

AActor* UALSXTCombatComponent::TraceForPotentialAttackTarget(float Distance)
{
	TArray<FHitResult> OutHits;
	FVector SweepStart = Character->GetActorLocation();
	FVector SweepEnd = Character->GetActorLocation() + (Character->GetActorForwardVector() * Distance);
	FCollisionShape MyColSphere = FCollisionShape::MakeSphere(50.0f);
	TArray<AActor*> InitialIgnoredActors;
	TArray<AActor*> OriginTraceIgnoredActors;
	InitialIgnoredActors.Add(Character);	// Add Self to Initial Trace Ignored Actors
	TArray<TEnumAsByte<EObjectTypeQuery>> AttackTraceObjectTypes;
	AttackTraceObjectTypes = CombatSettings.AttackTraceObjectTypes;
	bool isHit = UKismetSystemLibrary::SphereTraceMultiForObjects(GetWorld(), SweepStart, SweepEnd, 50, CombatSettings.AttackTraceObjectTypes, false, InitialIgnoredActors, EDrawDebugTrace::None, OutHits, true, FLinearColor::Green, FLinearColor::Red, 0.0f);

	if (isHit)
	{
		// loop through TArray
		for (auto& Hit : OutHits)
		{
			if (Hit.GetActor() != Character && UKismetSystemLibrary::DoesImplementInterface(Hit.GetActor(), UALSXTCombatInterface::StaticClass()))
			{
				if (GEngine && CombatSettings.DebugMode)
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Hit Result: %s"), *Hit.GetActor()->GetName()));
				}
				return Hit.GetActor();
			}
		}
	}
	return nullptr;
}

void UALSXTCombatComponent::Attack(const FGameplayTag& AttackType, const FGameplayTag& Strength, const float BaseDamage, const float PlayRate)
{
	FGameplayTag NewStance = ALSXTActionStanceTags::Standing;

	if (Character->GetLocomotionMode() == AlsLocomotionModeTags::InAir)
	{
		NewStance = ALSXTActionStanceTags::InAir;
	}
	else
	{
		if (Character->GetDesiredStance() == AlsStanceTags::Standing)
		{
			NewStance = ALSXTActionStanceTags::Standing;
		}
		else
		{
			NewStance = ALSXTActionStanceTags::Crouched;
		}
	}

	AActor* PotentialAttackTarget = TraceForPotentialAttackTarget(100.0f);
	FGameplayTag AttackMethod;
	if (IsValid(PotentialAttackTarget))
	{
		DetermineAttackMethod(AttackMethod, AttackType, NewStance, Strength, BaseDamage, PotentialAttackTarget);
	}
	else
	{
		AttackMethod = ALSXTAttackMethodTags::Regular;
	}

	if (AttackMethod == ALSXTAttackMethodTags::Regular || AttackMethod == ALSXTAttackMethodTags::Riposte)
	{
		StartAttack(AttackType, NewStance, Strength, BaseDamage, PlayRate, CombatSettings.bRotateToInputOnStart && Character->GetLocomotionState().bHasInput
			? Character->GetLocomotionState().InputYawAngle
			: UE_REAL_TO_FLOAT(FRotator::NormalizeAxis(Character->GetActorRotation().Yaw)));
	}
	else if (AttackMethod == ALSXTAttackMethodTags::Special || AttackMethod == ALSXTAttackMethodTags::TakeDown)
	{
		// int SynedAnimationIndex;
		// GetSyncedAttackMontageInfo(FSyncedActionMontageInfo & SyncedActionMontageInfo, const FGameplayTag & AttackType, int32 Index);
		StartSyncedAttack(Character->GetOverlayMode(), AttackType, NewStance, Strength, AttackMethod, BaseDamage, PlayRate, CombatSettings.bRotateToInputOnStart && Character->GetLocomotionState().bHasInput
			? Character->GetLocomotionState().InputYawAngle
			: UE_REAL_TO_FLOAT(FRotator::NormalizeAxis(Character->GetActorRotation().Yaw)), 0);
	}
}

void UALSXTCombatComponent::SetupInputComponent(UEnhancedInputComponent* PlayerInputComponent)
{
	if (PrimaryAction)
	{
		PlayerInputComponent->BindAction(PrimaryAction, ETriggerEvent::Triggered, this, &UALSXTCombatComponent::InputPrimaryAction);
	}
}

void UALSXTCombatComponent::InputPrimaryAction()
{
	if ((AlsCharacter->GetOverlayMode() == AlsOverlayModeTags::Default) && ((Character->GetCombatStance() == ALSXTCombatStanceTags::Ready) || (Character->GetCombatStance() == ALSXTCombatStanceTags::Aiming)) && CanAttack())
	{
		Attack(ALSXTAttackTypeTags::RightFist, ALSXTActionStrengthTags::Medium, 0.10f, 1.3f);
	}
}

bool UALSXTCombatComponent::IsAttackAllowedToStart(const UAnimMontage* Montage) const
{
	return !Character->GetLocomotionAction().IsValid() ||
		// ReSharper disable once CppRedundantParentheses
		(Character->GetLocomotionAction() == AlsLocomotionActionTags::PrimaryAction &&
			!Character->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Montage));
}


void UALSXTCombatComponent::StartAttack(const FGameplayTag& AttackType, const FGameplayTag& Stance, const FGameplayTag& Strength, const float BaseDamage, const float PlayRate, const float TargetYawAngle)
{
	if (Character->GetLocalRole() <= ROLE_SimulatedProxy)
	{
		return;
	}

	auto* Montage{ SelectAttackMontage(AttackType, Stance, Strength, BaseDamage) };

	if (!ALS_ENSURE(IsValid(Montage)) || !IsAttackAllowedToStart(Montage))
	{
		return;
	}

	const auto StartYawAngle{ UE_REAL_TO_FLOAT(FRotator::NormalizeAxis(Character->GetActorRotation().Yaw)) };

	// Clear the character movement mode and set the locomotion action to mantling.

	Character->SetMovementModeLocked(true);
	// Character->GetCharacterMovement()->SetMovementMode(MOVE_Custom);

	if (Character->GetLocalRole() >= ROLE_Authority)
	{
		// Character->GetCharacterMovement()->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
		// Character->GetMesh()->SetRelativeLocationAndRotation(BaseTranslationOffset, BaseRotationOffset);
		MulticastStartAttack(Montage, PlayRate, StartYawAngle, TargetYawAngle);
	}
	else
	{
		Character->GetCharacterMovement()->FlushServerMoves();

		StartAttackImplementation(Montage, PlayRate, StartYawAngle, TargetYawAngle);
		ServerStartAttack(Montage, PlayRate, StartYawAngle, TargetYawAngle);
		OnAttackStarted(AttackType, Stance, Strength, BaseDamage);
	}
}

void UALSXTCombatComponent::StartSyncedAttack(const FGameplayTag& Overlay, const FGameplayTag& AttackType, const FGameplayTag& Stance, const FGameplayTag& Strength, const FGameplayTag& AttackMode, const float BaseDamage, const float PlayRate, const float TargetYawAngle, int Index)
{

}

UALSXTCombatSettings* UALSXTCombatComponent::SelectAttackSettings_Implementation()
{
	return nullptr;
}

void UALSXTCombatComponent::DetermineAttackMethod_Implementation(FGameplayTag& AttackMethod, const FGameplayTag& AttackType, const FGameplayTag& Stance, const FGameplayTag& Strength, const float BaseDamage, const AActor* Target)
{
	if (LastTargets.Num() > 0)
	{
		for (auto& LastTarget : LastTargets)
		{
			if (LastTarget.Target == Target)
			{
				if (LastTarget.LastBlockedAttack < 2.0f)
				{
					AttackMethod = ALSXTAttackMethodTags::Riposte;
					return;
				}
				else if (LastTarget.ConsecutiveHits > 5)
				{
					AttackMethod = ALSXTAttackMethodTags::Special;
					return;
				}
				else
				{
					AttackMethod = ALSXTAttackMethodTags::Regular;
					return;
				}
			}
		}
	}
	else
	{
		AttackMethod = ALSXTAttackMethodTags::Regular;
	}
}

UAnimMontage* UALSXTCombatComponent::SelectAttackMontage_Implementation(const FGameplayTag& AttackType, const FGameplayTag& Stance, const FGameplayTag& Strength, const float BaseDamage)
{
	UAnimMontage* SelectedMontage{ nullptr };
	FActionMontageInfo LastAnimation{ nullptr };

	UALSXTCombatSettings* Settings = SelectAttackSettings();

	for (int i = 0; i < Settings->AttackTypes.Num(); ++i)
		{
			if (Settings->AttackTypes[i].AttackType == AttackType)
			{
				FAttackType FoundAttackType = Settings->AttackTypes[i];
				for (int j = 0; j < FoundAttackType.AttackStrengths.Num(); ++j)
				{
					if (FoundAttackType.AttackStrengths[j].AttackStrength == Strength)
					{
						FAttackStrength FoundAttackStrength = Settings->AttackTypes[i].AttackStrengths[j];
						for (int k = 0; k < FoundAttackStrength.AttackStances.Num(); ++k)
						{
							if (FoundAttackStrength.AttackStances[k].AttackStance == Stance)
							{
								FAttackStance FoundAttackStance = Settings->AttackTypes[i].AttackStrengths[j].AttackStances[k];
								// Declare Local Copy of Montages Array
								TArray<FActionMontageInfo> MontageArray{ FoundAttackStance.MontageInfo };

								if (MontageArray.Num() > 1)
								{
									// If LastAnimation exists, remove it from Local Montages array to avoid duplicates
									if (LastAnimation.Montage)
									{
										MontageArray.Remove(LastAnimation);
									}

									//Shuffle Array
									for (int m = MontageArray.Max(); m >= 0; --m)
									{
										int n = FMath::Rand() % (m + 1);
										if (m != n) MontageArray.Swap(m, n);
									}

									// Select Random Array Entry
									int RandIndex = rand() % MontageArray.Max();
									SelectedMontage = MontageArray[RandIndex].Montage;
									return SelectedMontage;
								}
								else
								{
									SelectedMontage = MontageArray[0].Montage;
									return SelectedMontage;
								}
							}
						}
					}
				}	
			}
		}
	return SelectedMontage;
}

void UALSXTCombatComponent::GetSyncedAttackMontageInfo_Implementation(FSyncedActionMontageInfo& SyncedActionMontageInfo, const FGameplayTag& AttackType, int32 Index)
{
	// UALSXTCombatSettings* Settings = SelectAttackSettings();
}

void UALSXTCombatComponent::ServerStartAttack_Implementation(UAnimMontage* Montage, const float PlayRate,
	const float StartYawAngle, const float TargetYawAngle)
{
	if (IsAttackAllowedToStart(Montage))
	{
		MulticastStartAttack(Montage, PlayRate, StartYawAngle, TargetYawAngle);
		Character->ForceNetUpdate();
	}
}

void UALSXTCombatComponent::MulticastStartAttack_Implementation(UAnimMontage* Montage, const float PlayRate,
	const float StartYawAngle, const float TargetYawAngle)
{
	StartAttackImplementation(Montage, PlayRate, StartYawAngle, TargetYawAngle);
}

void UALSXTCombatComponent::StartAttackImplementation(UAnimMontage* Montage, const float PlayRate,
	const float StartYawAngle, const float TargetYawAngle)
{
	
	if (IsAttackAllowedToStart(Montage) && Character->GetMesh()->GetAnimInstance()->Montage_Play(Montage, PlayRate))
	{
		CombatState.TargetYawAngle = TargetYawAngle;

		Character->ALSXTRefreshRotationInstant(StartYawAngle, ETeleportType::None);

		AlsCharacter->SetLocomotionAction(AlsLocomotionActionTags::PrimaryAction);
		// Crouch(); //Hack
	}
}

void UALSXTCombatComponent::RefreshAttack(const float DeltaTime)
{
	if (Character->GetLocomotionAction() != AlsLocomotionActionTags::PrimaryAction)
	{
		StopAttack();
		Character->ForceNetUpdate();
	}
	else
	{
		RefreshAttackPhysics(DeltaTime);
	}
}

void UALSXTCombatComponent::RefreshAttackPhysics(const float DeltaTime)
{
	// float Offset = CombatSettings->Combat.RotationOffset;
	auto ComponentRotation{ Character->GetCharacterMovement()->UpdatedComponent->GetComponentRotation() };
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	auto TargetRotation{ PlayerController->GetControlRotation() };
	// TargetRotation.Yaw = TargetRotation.Yaw + Offset;
	// TargetRotation.Yaw = TargetRotation.Yaw;
	// TargetRotation.Pitch = ComponentRotation.Pitch;
	// TargetRotation.Roll = ComponentRotation.Roll;

	// if (CombatSettings.RotationInterpolationSpeed <= 0.0f)
	// {
	// 	TargetRotation.Yaw = CombatState.TargetYawAngle;
	// 
	// 	Character->GetCharacterMovement()->MoveUpdatedComponent(FVector::ZeroVector, TargetRotation, false, nullptr, ETeleportType::TeleportPhysics);
	// }
	// else
	// {
	// 	TargetRotation.Yaw = UAlsMath::ExponentialDecayAngle(UE_REAL_TO_FLOAT(FRotator::NormalizeAxis(TargetRotation.Yaw)),
	// 		CombatState.TargetYawAngle, DeltaTime,
	// 		CombatSettings->Combat.RotationInterpolationSpeed);
	// 
	// 	Character->GetCharacterMovement()->MoveUpdatedComponent(FVector::ZeroVector, TargetRotation, false);
	// }
}

void UALSXTCombatComponent::StopAttack()
{
	if (Character->GetLocalRole() >= ROLE_Authority)
	{
		// Character->GetCharacterMovement()->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
	}

	if (Character->GetCharacterMovement()->MovementMode == EMovementMode::MOVE_Custom)
	{
		Character->SetMovementModeLocked(false);
		// Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		OnAttackEnded();
	}
	Character->SetMovementModeLocked(false);
}

void UALSXTCombatComponent::OnAttackEnded_Implementation() {}