// RagdollStabilityComponent.cpp

#include "RagdollStabilityComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "GameFramework/Actor.h"

URagdollStabilityComponent::URagdollStabilityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Query physics velocities after this frame's simulation has run, not before it -- avoids
	// reading last frame's numbers and reacting one tick late to fresh impacts.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bAutoActivate = true;
}

void URagdollStabilityComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	MeshComp = Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	PhysAnimComp = Owner ? Owner->FindComponentByClass<UPhysicalAnimationComponent>() : nullptr;

	if (!MeshComp || !PhysAnimComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("RagdollStabilityComponent on %s is missing a SkeletalMeshComponent or PhysicalAnimationComponent."),
			*GetNameSafe(Owner));
		SetComponentTickEnabled(false);
		return;
	}

	PhysAnimComp->SetSkeletalMeshComponent(MeshComp);

	// Required for OnComponentHit to fire from physics contacts (equivalent to ticking
	// "Simulation Generates Hit Events" on every body, done here so you don't have to remember
	// it per-asset).
	MeshComp->SetNotifyRigidBodyCollision(true);
	MeshComp->OnComponentHit.AddDynamic(this, &URagdollStabilityComponent::HandleMeshHit);

	VelocitySamples.Init(1.f, FMath::Max(SettleWindowSize, 1)); // seed as "not settled"

	ConfigureConstraintProjection();

	SetComponentTickEnabled(false); // dormant until StartRagdoll() is called
}

void URagdollStabilityComponent::ConfigureConstraintProjection()
{
	if (!bAutoEnableConstraintProjection || !MeshComp || !MeshComp->GetPhysicsAsset())
	{
		return;
	}

	// Best-effort runtime safety net, not a substitute for hand-tuning. The reliable place to set
	// this is per constraint in the Physics Asset Editor (Linear/Angular tab -> Enable Projection).
	// Live constraint instances mirror the asset's constraint setups 1:1 once physics state exists,
	// so this just guarantees projection is on even if an asset slips through un-tuned. Field names
	// below are stable across recent engine versions, but if this fails to compile against your
	// installed engine, check FConstraintProfileProperties in ConstraintInstance.h.
	for (FConstraintInstance* Constraint : MeshComp->Constraints)
	{
		if (Constraint)
		{
			Constraint->ProfileInstance.bEnableProjection = true;
			Constraint->ProfileInstance.ProjectionLinearAlpha = 1.f;
			Constraint->ProfileInstance.ProjectionAngularAlpha = 1.f;
		}
	}
}

void URagdollStabilityComponent::StartRagdoll(FName StartBone)
{
	if (!MeshComp || !PhysAnimComp)
	{
		return;
	}

	ActiveStartBone = StartBone;
	ContactAccumulator = 0.f;
	SettledForDuration = 0.f;
	VelocitySampleIndex = 0;
	VelocitySampleCount = 0;
	CurrentDriveMultiplier = 1.f;
	LastAppliedDriveMultiplier = -1.f; // force the first ApplyDrive call through

	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetAllBodiesBelowSimulatePhysics(StartBone, true);
	ApplyDrive(1.f);

	SetComponentTickEnabled(true);
	SetState(ERagdollState::BlendingIn);
}

void URagdollStabilityComponent::CancelRagdoll()
{
	if (!MeshComp)
	{
		return;
	}

	MeshComp->SetAllBodiesPhysicsBlendWeight(0.f);
	MeshComp->SetAllBodiesSimulatePhysics(false);
	SetState(ERagdollState::Done);
	SetComponentTickEnabled(false);
}

void URagdollStabilityComponent::SetState(ERagdollState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
	StateTime = 0.f;
	OnStateChanged.Broadcast(NewState);
}

void URagdollStabilityComponent::ApplyDrive(float Multiplier)
{
	if (!PhysAnimComp)
	{
		return;
	}
	// Skip redundant calls -- ApplyPhysicalAnimationSettingsBelow walks every body below the start
	// bone, so gating it against small changes avoids paying that cost every single tick.
	if (FMath::IsNearlyEqual(Multiplier, LastAppliedDriveMultiplier, 0.02f))
	{
		return;
	}

	FPhysicalAnimationData Data;
	Data.bIsLocalSimulation = false;
	Data.OrientationStrength = FullOrientationStrength * Multiplier;
	Data.AngularVelocityStrength = FullAngularVelocityStrength * Multiplier;
	Data.PositionStrength = FullPositionStrength * Multiplier;
	Data.VelocityStrength = FullVelocityStrength * Multiplier;
	Data.MaxLinearForce = 0.f;  // 0 = unclamped
	Data.MaxAngularForce = 0.f; // 0 = unclamped

	PhysAnimComp->ApplyPhysicalAnimationSettingsBelow(ActiveStartBone, Data, true);
	LastAppliedDriveMultiplier = Multiplier;
}

void URagdollStabilityComponent::HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	// Only count contacts while the ragdoll is actually under our control -- ignore stray hits
	// before StartRagdoll() or after we've fully blended back to animation.
	if (State != ERagdollState::Active && State != ERagdollState::BlendingIn)
	{
		return;
	}
	ContactAccumulator += NormalImpulse.Size();
}

float URagdollStabilityComponent::SampleMaxKeyBoneSpeed() const
{
	float Worst = 0.f;
	for (const FName& Bone : KeyBones)
	{
		const float LinRatio = MeshComp->GetPhysicsLinearVelocity(Bone).Size() / FMath::Max(SettleLinearVelocityThreshold, 1.f);
		const float AngRatio = MeshComp->GetPhysicsAngularVelocityInDegrees(Bone).Size() / FMath::Max(SettleAngularVelocityThreshold, 1.f);
		Worst = FMath::Max(Worst, FMath::Max(LinRatio, AngRatio));
	}
	return Worst; // <= 1.0 means every sampled bone is under its configured threshold
}

void URagdollStabilityComponent::PushVelocitySample(float Value)
{
	if (VelocitySamples.Num() == 0)
	{
		VelocitySamples.Init(1.f, FMath::Max(SettleWindowSize, 1));
	}
	VelocitySamples[VelocitySampleIndex] = Value;
	VelocitySampleIndex = (VelocitySampleIndex + 1) % VelocitySamples.Num();
	VelocitySampleCount = FMath::Min(VelocitySampleCount + 1, VelocitySamples.Num());
}

float URagdollStabilityComponent::GetRollingAverageVelocity() const
{
	if (VelocitySampleCount == 0)
	{
		return TNumericLimits<float>::Max();
	}
	float Sum = 0.f;
	for (int32 i = 0; i < VelocitySampleCount; ++i)
	{
		Sum += VelocitySamples[i];
	}
	return Sum / VelocitySampleCount;
}

void URagdollStabilityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	StateTime += DeltaTime;

	// Exponential decay so a burst of hits (landing on a stack) fades out smoothly rather than
	// snapping the drive strength back on the instant contacts stop being reported.
	ContactAccumulator *= FMath::Exp(-ContactImpulseDecayRate * DeltaTime);
	if (ContactAccumulator < 1.f)
	{
		ContactAccumulator = 0.f;
	}

	switch (State)
	{
	case ERagdollState::BlendingIn:
		TickBlendingIn(DeltaTime);
		break;
	case ERagdollState::Active:
		TickActive(DeltaTime);
		break;
	case ERagdollState::BlendingOut:
		TickBlendingOut(DeltaTime);
		break;
	default:
		break;
	}
}

void URagdollStabilityComponent::TickBlendingIn(float DeltaTime)
{
	const float Alpha = BlendInDuration > KINDA_SMALL_NUMBER ? FMath::Clamp(StateTime / BlendInDuration, 0.f, 1.f) : 1.f;
	const float Weight = BlendInCurve ? BlendInCurve->GetFloatValue(Alpha) : Alpha;
	MeshComp->SetAllBodiesPhysicsBlendWeight(FMath::Clamp(Weight, 0.f, 1.f));

	if (Alpha >= 1.f)
	{
		SetState(ERagdollState::Active);
	}
}

void URagdollStabilityComponent::TickActive(float DeltaTime)
{
	// Core fix #1: back the drive off the moment contact gets messy, instead of letting a fixed
	// curve keep pulling toward the death pose while the collision solver is still fighting for
	// position. Smoothly interpolated so the transition itself doesn't add a visible pop.
	const float TargetMultiplier = ContactAccumulator > ContactImpulseThreshold ? ContactDriveMultiplier : 1.f;
	CurrentDriveMultiplier = FMath::FInterpTo(CurrentDriveMultiplier, TargetMultiplier, DeltaTime, DriveMultiplierInterpSpeed);
	ApplyDrive(CurrentDriveMultiplier);

	// Core fix #2: judge "settled" from a smoothed rolling window instead of the engine's native
	// sleep flag, so resting-contact chatter can't indefinitely postpone the blend-out.
	PushVelocitySample(SampleMaxKeyBoneSpeed());
	const bool bLooksSettled = GetRollingAverageVelocity() <= 1.f;
	SettledForDuration = bLooksSettled ? SettledForDuration + DeltaTime : 0.f;

	const bool bNaturallySettled = SettledForDuration >= SettleSustainDuration;
	const bool bStuckTimedOut = StateTime >= StuckTimeout;

	if (bNaturallySettled || bStuckTimedOut)
	{
		// Settling is intentionally instantaneous: its only job is to give state-change listeners
		// (UI, debug overlays) a clean marker between "was active" and "is blending out", and to
		// give OnSettled a well-defined moment for your pose snapshot.
		SetState(ERagdollState::Settling);
		OnSettled.Broadcast();
		SetState(ERagdollState::BlendingOut);
	}
}

void URagdollStabilityComponent::TickBlendingOut(float DeltaTime)
{
	const float Alpha = BlendOutDuration > KINDA_SMALL_NUMBER ? FMath::Clamp(StateTime / BlendOutDuration, 0.f, 1.f) : 1.f;
	const float Weight = BlendOutCurve ? BlendOutCurve->GetFloatValue(Alpha) : (1.f - Alpha);
	MeshComp->SetAllBodiesPhysicsBlendWeight(FMath::Clamp(Weight, 0.f, 1.f));

	if (Alpha >= 1.f)
	{
		MeshComp->SetAllBodiesPhysicsBlendWeight(0.f);
		SetState(ERagdollState::Done);
		OnFinished.Broadcast();
		SetComponentTickEnabled(false);
	}
}
