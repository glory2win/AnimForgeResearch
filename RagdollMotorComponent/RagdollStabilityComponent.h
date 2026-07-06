// RagdollStabilityComponent.h
//
// Sits alongside your existing PhysicalAnimationComponent + SkeletalMeshComponent and takes over
// the "death -> ragdoll -> settle -> blend back" pipeline you already have, but makes two things
// context-aware instead of blind-curve-driven:
//
//   1. Physical Animation drive strength backs off automatically while the ragdoll is in a messy,
//      high-contact situation (landed on another ragdoll / a physics prop), instead of fighting
//      the collision solver for the whole duration of your curve.
//   2. The blend-out trigger is decoupled from the engine's native physics sleep state. It watches
//      a small rolling window of key-bone velocities and fires as soon as things LOOK settled,
//      with a hard timeout fallback for corner-stuck cases that would otherwise never sleep cleanly.
//
// Wiring:
//   - Add this component next to your PhysicalAnimationComponent on the same actor.
//   - Call StartRagdoll() from your death AnimNotify (replaces your current "ragdoll start" call).
//   - Bind OnSettled in your AnimBP/controller to do your pose snapshot -- this fires exactly once,
//     right before the blend-out ramp begins.
//   - Bind OnFinished if you need to know when physics blend weight has fully returned to 0.
//
// Also assumes:
//   - Your Physics Asset has "Enable Projection" set on constraints prone to being pushed to their
//     limits (this component sets it as a runtime safety net too, see ConfigureConstraintProjection).
//   - Ragdoll-vs-ragdoll and ragdoll-vs-environment collision channels are already set up sanely
//     (see accompanying discussion on collision complexity) -- this component doesn't touch that.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Curves/CurveFloat.h"
#include "RagdollStabilityComponent.generated.h"

class USkeletalMeshComponent;
class UPhysicalAnimationComponent;
class UPrimitiveComponent;
struct FHitResult;

UENUM(BlueprintType)
enum class ERagdollState : uint8
{
	Inactive,
	BlendingIn,
	Active,
	Settling,
	BlendingOut,
	Done
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRagdollStateChanged, ERagdollState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRagdollSettled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRagdollFinished);

// Replace YOURGAME_API with your project's generated API macro (e.g. MYGAME_API).
UCLASS(ClassGroup = (Physics), meta = (BlueprintSpawnableComponent))
class YOURGAME_API URagdollStabilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URagdollStabilityComponent();

	// Call from your death AnimNotify. StartBone is usually "pelvis" (whatever you pass to
	// SetAllBodiesBelowSimulatePhysics today).
	UFUNCTION(BlueprintCallable, Category = "Ragdoll")
	void StartRagdoll(FName StartBone = NAME_None);

	// Immediately snaps back to full animation control (respawn, cleanup, pickup, etc).
	UFUNCTION(BlueprintCallable, Category = "Ragdoll")
	void CancelRagdoll();

	UFUNCTION(BlueprintPure, Category = "Ragdoll")
	ERagdollState GetState() const { return State; }

	UPROPERTY(BlueprintAssignable, Category = "Ragdoll")
	FOnRagdollStateChanged OnStateChanged;

	// Fires once, right before the blend-out ramp starts. Bind your pose-snapshot logic here.
	UPROPERTY(BlueprintAssignable, Category = "Ragdoll")
	FOnRagdollSettled OnSettled;

	// Fires once physics blend weight has fully returned to 0.
	UPROPERTY(BlueprintAssignable, Category = "Ragdoll")
	FOnRagdollFinished OnFinished;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---------------- Setup ----------------

	// Bones sampled each tick to judge whether the ragdoll has settled. Keep this short --
	// extremities (hands, feet, head) show residual jitter first and are the cheapest tell.
	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Setup")
	TArray<FName> KeyBones = { TEXT("pelvis"), TEXT("head"), TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r") };

	// If true, forces projection on for every live constraint at BeginPlay so a corner-stuck joint
	// corrects positionally instead of via velocity. The authoritative place to set this is per
	// constraint in the Physics Asset Editor -- this is just a safety net. See .cpp for caveats.
	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Setup")
	bool bAutoEnableConstraintProjection = true;

	// ---------------- Blend curves (X = 0..1 normalized time, Y = physics blend weight 0..1) ----------------

	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Curves")
	UCurveFloat* BlendInCurve = nullptr;

	// Author this descending (Y goes 1 -> 0) since it's applied directly as the blend weight.
	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Curves")
	UCurveFloat* BlendOutCurve = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Timing")
	float BlendInDuration = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Timing")
	float BlendOutDuration = 0.5f;

	// ---------------- Physical Animation drive strength while stable ----------------

	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Drive")
	float FullOrientationStrength = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Drive")
	float FullAngularVelocityStrength = 15.f;

	// World-space; leave at 0 unless you specifically need positional pull -- it fights collision
	// response even harder than orientation strength does.
	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Drive")
	float FullPositionStrength = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Drive")
	float FullVelocityStrength = 0.f;

	// Multiplier applied to all drive strengths above while in "complex contact". 0 = fully passive
	// ragdoll (let the collision solver alone resolve it), 1 = no reduction at all.
	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Drive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ContactDriveMultiplier = 0.1f;

	// How fast the live multiplier chases its target, per second. Higher = snappier reaction to
	// contact, lower = smoother but slightly slower to back off.
	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Drive")
	float DriveMultiplierInterpSpeed = 6.f;

	// ---------------- Contact detection ----------------

	// Decayed cumulative impulse magnitude above which we consider the ragdoll "in complex contact".
	// Tune by logging ContactAccumulator during a normal single-body landing vs. a stack/pileup.
	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Contact")
	float ContactImpulseThreshold = 300.f;

	// Exponential decay rate (per second) applied to the impulse accumulator every tick.
	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Contact")
	float ContactImpulseDecayRate = 4.f;

	// ---------------- Settle detection ----------------

	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Settle")
	float SettleLinearVelocityThreshold = 40.f; // cm/s

	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Settle")
	float SettleAngularVelocityThreshold = 60.f; // deg/s

	// How long the rolling-average speed must stay under threshold before we call it settled.
	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Settle")
	float SettleSustainDuration = 0.25f;

	// Rolling window size in samples (~ticks). Smooths the settle check against contact chatter
	// so a single noisy frame doesn't reset the sustain timer.
	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Settle")
	int32 SettleWindowSize = 12;

	// Hard ceiling: if we haven't settled naturally by this time since entering Active, force the
	// blend-out anyway. This is what saves you from the corner-stuck case that never truly sleeps.
	UPROPERTY(EditDefaultsOnly, Category = "Ragdoll|Settle")
	float StuckTimeout = 4.f;

private:
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> MeshComp = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalAnimationComponent> PhysAnimComp = nullptr;

	ERagdollState State = ERagdollState::Inactive;
	FName ActiveStartBone = NAME_None;

	float StateTime = 0.f;
	float ContactAccumulator = 0.f;
	float CurrentDriveMultiplier = 1.f;
	float LastAppliedDriveMultiplier = -1.f;
	float SettledForDuration = 0.f;

	// Fixed-size ring buffer -- no per-tick heap churn.
	TArray<float> VelocitySamples;
	int32 VelocitySampleIndex = 0;
	int32 VelocitySampleCount = 0;

	UFUNCTION()
	void HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	void SetState(ERagdollState NewState);
	void ConfigureConstraintProjection();
	void ApplyDrive(float Multiplier);

	float SampleMaxKeyBoneSpeed() const;
	void PushVelocitySample(float Value);
	float GetRollingAverageVelocity() const;

	void TickBlendingIn(float DeltaTime);
	void TickActive(float DeltaTime);
	void TickBlendingOut(float DeltaTime);
};
