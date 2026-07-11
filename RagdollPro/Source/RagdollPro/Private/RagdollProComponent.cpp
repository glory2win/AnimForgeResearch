// Copyright AnimForge Studios. All Rights Reserved.

#include "RagdollProComponent.h"
#include "RagdollPro.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

URagdollProComponent::URagdollProComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Query physics velocities after this frame's simulation has run, not before it -- avoids
	// reading last frame's numbers and reacting one tick late to fresh impacts.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bAutoActivate = true;
}

void URagdollProComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	MeshComp = Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr;

	if (!MeshComp)
	{
		UE_LOG(LogRagdollPro, Warning, TEXT("RagdollProComponent on %s found no SkeletalMeshComponent."), *GetNameSafe(Owner));
		SetComponentTickEnabled(false);
		return;
	}

	// Required for OnComponentHit to fire from physics contacts (equivalent to ticking
	// "Simulation Generates Hit Events" on every body, done here so you don't have to remember
	// it per-asset).
	MeshComp->SetNotifyRigidBodyCollision(true);
	MeshComp->OnComponentHit.AddDynamic(this, &URagdollProComponent::HandleMeshHit);

	VelocitySamples.Init(1.f, FMath::Max(SettleWindowSize, 1)); // seed as "not settled"

	SetComponentTickEnabled(false); // dormant until StartRagdoll()/StartHitReaction()
}

// ---------------------------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------------------------

void URagdollProComponent::StartRagdoll(FName StartBone)
{
	if (!MeshComp)
	{
		return;
	}

	bHitReaction = false;
	ActiveStartBone = StartBone;
	ResetRuntimeState();
	PrepareMeshForSimulation();

	if (StartBone.IsNone())
	{
		MeshComp->SetAllBodiesSimulatePhysics(true);
	}
	else
	{
		MeshComp->SetAllBodiesBelowSimulatePhysics(StartBone, true, true);
	}

	BuildMotorTable();
	ConfigureConstraintProjection();
	MeshComp->bUpdateJointsFromAnimation = bDriveTargetsFromAnimation;
	ApplyMotorStrength(ComposeGlobalScale(), /*bForce =*/ true);

	SetComponentTickEnabled(true);
	SetState(ERagdollProState::BlendingIn);
}

void URagdollProComponent::StartHitReaction(FName HitBone, FVector Impulse, float Duration)
{
	if (!MeshComp || HitBone.IsNone())
	{
		return;
	}
	// A full ragdoll owns the body; a hit reaction never interrupts it. (The reverse escalation
	// -- StartRagdoll during a hit reaction -- is allowed and just re-initializes.)
	if (State != ERagdollProState::Inactive && State != ERagdollProState::Done)
	{
		return;
	}

	bHitReaction = true;
	ActiveStartBone = HitBone;
	ActiveHitReactionDuration = Duration > 0.f ? Duration : HitReactionDuration;
	ResetRuntimeState();
	PrepareMeshForSimulation();

	MeshComp->SetAllBodiesBelowSimulatePhysics(HitBone, true, true);

	BuildMotorTable();
	ConfigureConstraintProjection();
	MeshComp->bUpdateJointsFromAnimation = bDriveTargetsFromAnimation;
	ApplyMotorStrength(ComposeGlobalScale(), /*bForce =*/ true);

	if (!Impulse.IsNearlyZero())
	{
		ApplyImpulseToBone(HitBone, Impulse, true);
	}

	SetComponentTickEnabled(true);
	SetState(ERagdollProState::BlendingIn);
}

void URagdollProComponent::CancelRagdoll()
{
	if (!MeshComp || State == ERagdollProState::Inactive)
	{
		return;
	}
	FinishRagdoll(/*bBroadcastFinished =*/ false);
}

void URagdollProComponent::FreezeMotorTargets()
{
	if (MeshComp)
	{
		// Drive targets simply stop being refreshed; the joints keep pulling toward whatever the
		// animation pose was on the last updated frame.
		MeshComp->bUpdateJointsFromAnimation = false;
	}
}

void URagdollProComponent::UnfreezeMotorTargets()
{
	if (MeshComp && bDriveTargetsFromAnimation)
	{
		MeshComp->bUpdateJointsFromAnimation = true;
	}
}

void URagdollProComponent::SetGlobalMotorScale(float Scale)
{
	GlobalMotorScale = FMath::Max(Scale, 0.f);
}

void URagdollProComponent::ApplyImpulseToBone(FName Bone, FVector Impulse, bool bVelChange, int32 PropagateDepth, float PropagateFalloff)
{
	if (!MeshComp)
	{
		return;
	}

	MeshComp->AddImpulse(Impulse, Bone, bVelChange);

	// Walk up the parent chain with geometric falloff. Bones without a simulating body just
	// no-op inside AddImpulse, so skipped bodies in sparse physics assets are fine.
	FName Current = Bone;
	float Scale = 1.f;
	for (int32 Depth = 0; Depth < PropagateDepth; ++Depth)
	{
		Current = MeshComp->GetParentBone(Current);
		if (Current.IsNone())
		{
			break;
		}
		Scale *= PropagateFalloff;
		MeshComp->AddImpulse(Impulse * Scale, Current, bVelChange);
	}
}

bool URagdollProComponent::IsLyingFaceDown() const
{
	if (!MeshComp)
	{
		return false;
	}
	const FQuat PelvisQuat = MeshComp->GetBoneQuaternion(PelvisBoneName);
	return FVector::DotProduct(PelvisQuat.RotateVector(PelvisLocalChestAxis.GetSafeNormal()), FVector::UpVector) < 0.f;
}

FVector URagdollProComponent::GetPelvisWorldLocation() const
{
	return MeshComp ? MeshComp->GetBoneLocation(PelvisBoneName) : FVector::ZeroVector;
}

// ---------------------------------------------------------------------------------------------
// Setup / teardown internals
// ---------------------------------------------------------------------------------------------

void URagdollProComponent::ResetRuntimeState()
{
	StateTime = 0.f;
	RagdollTime = 0.f;
	ContactAccumulator = 0.f;
	CurrentContactMultiplier = 1.f;
	LastAppliedScale = -1.f; // force the first ApplyMotorStrength through
	CurrentBlendWeight = 0.f;
	SettledForDuration = 0.f;
	VelocitySampleIndex = 0;
	VelocitySampleCount = 0;
}

void URagdollProComponent::PrepareMeshForSimulation()
{
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	if (bManageAnimTickOption)
	{
		// The motor targets ARE the anim pose -- if pose evaluation stops when off-screen, every
		// off-screen ragdoll drives toward a stale pose. Force full evaluation for the duration.
		SavedAnimTickOption = static_cast<uint8>(MeshComp->VisibilityBasedAnimTickOption);
		MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
}

void URagdollProComponent::RestoreAnimSettings()
{
	if (bManageAnimTickOption && MeshComp)
	{
		MeshComp->VisibilityBasedAnimTickOption = static_cast<EVisibilityBasedAnimTickOption>(SavedAnimTickOption);
	}
}

void URagdollProComponent::FinishRagdoll(bool bBroadcastFinished)
{
	SetBlendWeightBelow(0.f);

	if (ActiveStartBone.IsNone())
	{
		MeshComp->SetAllBodiesSimulatePhysics(false);
	}
	else
	{
		MeshComp->SetAllBodiesBelowSimulatePhysics(ActiveStartBone, false, true);
	}

	StopAllMotors();
	MotorTable.Reset();
	MeshComp->bUpdateJointsFromAnimation = false;
	RestoreAnimSettings();

	SetState(ERagdollProState::Done);
	if (bBroadcastFinished)
	{
		OnFinished.Broadcast();
	}
	SetComponentTickEnabled(false);
}

void URagdollProComponent::BuildMotorTable()
{
	MotorTable.Reset();

	for (FConstraintInstance* Constraint : MeshComp->Constraints)
	{
		if (!Constraint)
		{
			continue;
		}

		// JointName is the child bone of the constraint; only drive joints inside the simulated
		// branch, otherwise we'd be pushing torque into kinematic bodies.
		const FName JointBone = Constraint->JointName;
		if (!ActiveStartBone.IsNone() && JointBone != ActiveStartBone && !MeshComp->BoneIsChildOf(JointBone, ActiveStartBone))
		{
			continue;
		}

		FMotorEntry Entry;
		Entry.Constraint = Constraint;
		ResolveBoneMultipliers(JointBone, Entry.SpringMul, Entry.DampingMul);

		// SLERP mode drives the full 3D rotation toward the target in one spring instead of
		// separate twist/swing springs -- markedly more stable for humanoid pose matching.
		// NOTE: silently ignored on joints whose angular DOFs are all Locked in the asset.
		// The math: Docs/02_JointDriveMath.md.
		Constraint->SetAngularDriveMode(EAngularDriveMode::SLERP);
		Constraint->SetOrientationDriveSLERP(true);
		Constraint->SetAngularVelocityDriveSLERP(true);

		MotorTable.Add(Entry);
	}

	if (MotorTable.Num() == 0)
	{
		UE_LOG(LogRagdollPro, Warning, TEXT("RagdollProComponent on %s built an empty motor table below bone '%s' -- check the Physics Asset."),
			*GetNameSafe(GetOwner()), *ActiveStartBone.ToString());
	}
}

void URagdollProComponent::ResolveBoneMultipliers(FName Bone, float& OutSpringMul, float& OutDampingMul) const
{
	OutSpringMul = 1.f;
	OutDampingMul = 1.f;

	// Exact match beats any inherited profile.
	for (const FRagdollProBoneProfile& Profile : BoneProfiles)
	{
		if (Profile.BoneName == Bone)
		{
			OutSpringMul = Profile.SpringMultiplier;
			OutDampingMul = Profile.DampingMultiplier;
			return;
		}
	}

	// Otherwise the nearest ancestor with bIncludeChildren wins.
	FName Current = MeshComp->GetParentBone(Bone);
	while (!Current.IsNone())
	{
		for (const FRagdollProBoneProfile& Profile : BoneProfiles)
		{
			if (Profile.bIncludeChildren && Profile.BoneName == Current)
			{
				OutSpringMul = Profile.SpringMultiplier;
				OutDampingMul = Profile.DampingMultiplier;
				return;
			}
		}
		Current = MeshComp->GetParentBone(Current);
	}
}

void URagdollProComponent::ConfigureConstraintProjection()
{
	if (!bAutoEnableConstraintProjection)
	{
		return;
	}

	// Best-effort runtime safety net, not a substitute for hand-tuning in the Physics Asset
	// Editor. Field names are stable across recent engine versions; if this fails to compile
	// against your installed engine, check FConstraintProfileProperties in ConstraintInstance.h.
	for (const FMotorEntry& Entry : MotorTable)
	{
		Entry.Constraint->ProfileInstance.bEnableProjection = true;
		Entry.Constraint->ProfileInstance.ProjectionLinearAlpha = 1.f;
		Entry.Constraint->ProfileInstance.ProjectionAngularAlpha = 1.f;
	}
}

// ---------------------------------------------------------------------------------------------
// Motor strength
// ---------------------------------------------------------------------------------------------

float URagdollProComponent::ComposeGlobalScale() const
{
	float Scale = GlobalMotorScale * CurrentContactMultiplier;

	if (StrengthOverLifetimeCurve)
	{
		Scale *= FMath::Max(StrengthOverLifetimeCurve->GetFloatValue(RagdollTime), 0.f);
	}
	if (bHitReaction)
	{
		Scale *= HitReactionStrengthMultiplier;
	}
	// Relax the motors in step with the blend-out so the joints stop arguing with the animation
	// that is taking over.
	if (State == ERagdollProState::BlendingOut)
	{
		Scale *= CurrentBlendWeight;
	}
	return FMath::Max(Scale, 0.f);
}

void URagdollProComponent::ApplyMotorStrength(float Scale, bool bForce)
{
	// Gate small changes: SetAngularDriveParams pushes into the physics scene per constraint, so
	// skipping sub-2% deltas keeps the steady state free.
	if (!bForce && FMath::IsNearlyEqual(Scale, LastAppliedScale, 0.02f))
	{
		return;
	}

	// Damping decays on a square-root curve relative to spring: this keeps the damping RATIO
	// (zeta) constant as strength scales, so limbs go compliant without also going bouncy.
	// Full derivation: Docs/03_SpringDamperTheory.md, section "Why damping scales as sqrt(s)".
	const float DampingScale = FMath::Sqrt(FMath::Max(Scale, 0.f));

	for (const FMotorEntry& Entry : MotorTable)
	{
		Entry.Constraint->SetAngularDriveParams(
			BaseAngularSpring * Entry.SpringMul * Scale,
			BaseAngularDamping * Entry.DampingMul * DampingScale,
			MaxAngularForce);
	}
	LastAppliedScale = Scale;
}

void URagdollProComponent::StopAllMotors()
{
	for (const FMotorEntry& Entry : MotorTable)
	{
		Entry.Constraint->SetAngularDriveParams(0.f, 0.f, 0.f);
		Entry.Constraint->SetOrientationDriveSLERP(false);
		Entry.Constraint->SetAngularVelocityDriveSLERP(false);
	}
	LastAppliedScale = -1.f;
}

void URagdollProComponent::UpdateContactMultiplier(float DeltaTime)
{
	// Back the motors off the moment contact gets messy, instead of holding the pose while the
	// collision solver is still fighting for position. Smoothly interpolated so the transition
	// itself doesn't add a visible pop.
	const float Target = ContactAccumulator > ContactImpulseThreshold ? ContactDriveMultiplier : 1.f;
	CurrentContactMultiplier = FMath::FInterpTo(CurrentContactMultiplier, Target, DeltaTime, DriveMultiplierInterpSpeed);
}

void URagdollProComponent::HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	// Only count contacts while the ragdoll is actually under our control -- ignore stray hits
	// before Start*() or after we've fully blended back to animation.
	if (State != ERagdollProState::Active && State != ERagdollProState::BlendingIn)
	{
		return;
	}
	ContactAccumulator += NormalImpulse.Size();
}

// ---------------------------------------------------------------------------------------------
// Blend weight / settle detection
// ---------------------------------------------------------------------------------------------

void URagdollProComponent::SetBlendWeightBelow(float Weight)
{
	Weight = FMath::Clamp(Weight, 0.f, 1.f);
	CurrentBlendWeight = Weight;

	if (ActiveStartBone.IsNone())
	{
		MeshComp->SetAllBodiesPhysicsBlendWeight(Weight);
	}
	else
	{
		// Branch-scoped so hit reactions only blend the affected limb.
		MeshComp->SetAllBodiesBelowPhysicsBlendWeight(ActiveStartBone, Weight, false, true);
	}
}

float URagdollProComponent::SampleMaxKeyBoneSpeed() const
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

void URagdollProComponent::PushVelocitySample(float Value)
{
	if (VelocitySamples.Num() == 0)
	{
		VelocitySamples.Init(1.f, FMath::Max(SettleWindowSize, 1));
	}
	VelocitySamples[VelocitySampleIndex] = Value;
	VelocitySampleIndex = (VelocitySampleIndex + 1) % VelocitySamples.Num();
	VelocitySampleCount = FMath::Min(VelocitySampleCount + 1, VelocitySamples.Num());
}

float URagdollProComponent::GetRollingAverageVelocity() const
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

// ---------------------------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------------------------

void URagdollProComponent::SetState(ERagdollProState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
	StateTime = 0.f;
	OnStateChanged.Broadcast(NewState);
}

void URagdollProComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	StateTime += DeltaTime;
	RagdollTime += DeltaTime;

	// Exponential decay so a burst of hits (landing on a stack) fades out smoothly rather than
	// snapping the motor strength back on the instant contacts stop being reported.
	// Frame-rate independence proof: Docs/04_ContactBackoffMath.md.
	ContactAccumulator *= FMath::Exp(-ContactImpulseDecayRate * DeltaTime);
	if (ContactAccumulator < 1.f)
	{
		ContactAccumulator = 0.f;
	}

	switch (State)
	{
	case ERagdollProState::BlendingIn:
		TickBlendingIn(DeltaTime);
		break;
	case ERagdollProState::Active:
		TickActive(DeltaTime);
		break;
	case ERagdollProState::BlendingOut:
		TickBlendingOut(DeltaTime);
		break;
	default:
		break;
	}

#if ENABLE_DRAW_DEBUG
	if (bDrawDebug)
	{
		DrawDebugInfo();
	}
#endif
}

void URagdollProComponent::TickBlendingIn(float DeltaTime)
{
	const float Duration = GetActiveBlendInDuration();
	const float Alpha = Duration > KINDA_SMALL_NUMBER ? FMath::Clamp(StateTime / Duration, 0.f, 1.f) : 1.f;
	const float Weight = BlendInCurve && !bHitReaction ? BlendInCurve->GetFloatValue(Alpha) : Alpha;
	SetBlendWeightBelow(Weight);

	UpdateContactMultiplier(DeltaTime);
	ApplyMotorStrength(ComposeGlobalScale());

	if (Alpha >= 1.f)
	{
		SetState(ERagdollProState::Active);
	}
}

void URagdollProComponent::TickActive(float DeltaTime)
{
	UpdateContactMultiplier(DeltaTime);
	ApplyMotorStrength(ComposeGlobalScale());

	if (bHitReaction)
	{
		// Hit reactions recover on a timer, not on settle detection -- the branch is powered and
		// tracking animation the whole time, so "settled" is meaningless here.
		if (StateTime >= ActiveHitReactionDuration)
		{
			SetState(ERagdollProState::BlendingOut);
		}
		return;
	}

	if (bFollowPelvisWithOwner)
	{
		UpdateOwnerFollow();
	}

	// Judge "settled" from a smoothed rolling window instead of the engine's native sleep flag,
	// so resting-contact chatter can't indefinitely postpone the blend-out.
	PushVelocitySample(SampleMaxKeyBoneSpeed());
	const bool bLooksSettled = GetRollingAverageVelocity() <= 1.f;
	SettledForDuration = bLooksSettled ? SettledForDuration + DeltaTime : 0.f;

	const bool bNaturallySettled = SettledForDuration >= SettleSustainDuration;
	const bool bStuckTimedOut = StateTime >= StuckTimeout;

	if (bNaturallySettled || bStuckTimedOut)
	{
		// Settling is intentionally instantaneous: its only job is to give state-change listeners
		// a clean marker between "was active" and "is blending out", and to give OnSettled a
		// well-defined moment for your pose snapshot.
		SetState(ERagdollProState::Settling);
		OnSettled.Broadcast();
		SetState(ERagdollProState::BlendingOut);
	}
}

void URagdollProComponent::TickBlendingOut(float DeltaTime)
{
	const float Duration = GetActiveBlendOutDuration();
	const float Alpha = Duration > KINDA_SMALL_NUMBER ? FMath::Clamp(StateTime / Duration, 0.f, 1.f) : 1.f;
	const float Weight = BlendOutCurve && !bHitReaction ? BlendOutCurve->GetFloatValue(Alpha) : (1.f - Alpha);
	SetBlendWeightBelow(Weight);

	// ComposeGlobalScale folds CurrentBlendWeight in during this state, so the joints relax in
	// lockstep with the animation taking over.
	ApplyMotorStrength(ComposeGlobalScale());

	if (Alpha >= 1.f)
	{
		FinishRagdoll(/*bBroadcastFinished =*/ true);
	}
}

// ---------------------------------------------------------------------------------------------
// Owner follow / debug
// ---------------------------------------------------------------------------------------------

void URagdollProComponent::UpdateOwnerFollow()
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	// If the mesh IS the root, moving it would drag the (blended) target pose around mid-simulation.
	if (!Root || Root == MeshComp)
	{
		return;
	}

	const FVector Pelvis = MeshComp->GetBoneLocation(PelvisBoneName);

	FVector FloorZ = Pelvis;
	FHitResult Hit;
	FCollisionQueryParams Params(FName(TEXT("RagdollProOwnerFollow")), false, Owner);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Pelvis, Pelvis - FVector(0.f, 0.f, OwnerFollowTraceDistance), OwnerFollowTraceChannel, Params))
	{
		FloorZ = Hit.Location;
	}

	// Invisible while blend weight is 1: simulated bones live in world space, so shifting the
	// component transform under them doesn't move anything on screen.
	Owner->SetActorLocation(FVector(Pelvis.X, Pelvis.Y, FloorZ.Z + OwnerFollowZOffset), false, nullptr, ETeleportType::TeleportPhysics);
}

void URagdollProComponent::DrawDebugInfo() const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World || !MeshComp)
	{
		return;
	}

	const FVector Pelvis = GetPelvisWorldLocation();
	const FString Text = FString::Printf(TEXT("State=%d  Motor=%.2f  Contact=%.0f  Settled=%.2fs"),
		static_cast<int32>(State), GetCurrentMotorScale(), ContactAccumulator, SettledForDuration);
	DrawDebugString(World, Pelvis + FVector(0.f, 0.f, 40.f), Text, nullptr, FColor::White, 0.f, true);

	for (const FName& Bone : KeyBones)
	{
		const float LinRatio = MeshComp->GetPhysicsLinearVelocity(Bone).Size() / FMath::Max(SettleLinearVelocityThreshold, 1.f);
		const FColor Color = LinRatio <= 1.f ? FColor::Green : FColor::Red;
		DrawDebugSphere(World, MeshComp->GetBoneLocation(Bone), 4.f, 8, Color, false, -1.f, 0, 0.5f);
	}
#endif
}
