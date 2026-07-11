// Copyright AnimForge Studios. All Rights Reserved.
//
// URagdollProComponent
//
// Constraint-native powered ragdoll controller -- the flagship component of RagdollPro and a
// full replacement for the PhysicalAnimationComponent pipeline.
//
// Instead of bolting a world-space drive constraint onto every body and pulling each bone toward
// its animated WORLD transform, this powers the angular SLERP drives already inside each Physics
// Asset constraint, so every joint applies torque toward its animated PARENT-RELATIVE rotation --
// muscles across a joint, not strings from the sky.
//
// Why this behaves better than Physical Animation for pileup/settle problems:
//   - Parent-relative targets can't fight world-space collision response. When the ragdoll lands
//     on a stack, joints still try to hold the pose SHAPE, but nothing drags bones toward world
//     positions that are now inside other bodies.
//   - MaxAngularForce gives each joint a physically bounded torque budget ("muscle strength")
//     instead of an unclamped corrective force.
//   - No extra constraint instances per body -> cheaper, and one fewer component on the actor.
//
// Feature set:
//   - State machine: Inactive -> BlendingIn -> Active -> Settling -> BlendingOut -> Done,
//     with BlueprintAssignable delegates for each transition point.
//   - Motor targets streamed from the live animation pose (engine bUpdateJointsFromAnimation
//     path); FreezeMotorTargets() locks them at any moment (e.g. hold the death pose).
//   - Per-bone-chain strength profiles (strong spine, weak limbs) resolved by hierarchy walk.
//   - Contact-aware strength backoff via a decaying impulse accumulator.
//   - Optional strength-over-lifetime curve (fade to fully limp over N seconds).
//   - Velocity-window settle detection + hard stuck timeout (never waits on physics sleep).
//   - Partial-body hit reactions: StartHitReaction() simulates one branch, boosts motors,
//     applies the impulse, auto-recovers on a timer.
//   - ApplyImpulseToBone() with parent-chain propagation falloff.
//   - Optional owner-follow: keeps the actor root over the pelvis for camera + getup.
//   - Getup helpers: IsLyingFaceDown(), GetPelvisWorldLocation().
//   - Constraint projection safety net + debug draw.
//
// Wiring:
//   - Needs only a SkeletalMeshComponent on the same actor. No PhysicalAnimationComponent.
//   - Call StartRagdoll() from your death AnimNotify, StartHitReaction() from damage handling.
//   - Bind OnSettled for your pose snapshot, OnFinished for end-of-blend cleanup.
//
// Physics Asset requirements:
//   - Angular swing/twist on driven joints must be Limited (or Free), NOT Locked -- SLERP drives
//     are silently ignored on fully locked joints.
//   - Keep "Enable Projection" on pileup-prone constraints (forced at runtime here too as a
//     safety net).
//
// The math behind every tunable on this component is derived in /Docs:
//   Docs/02_JointDriveMath.md      -- SLERP drive as a PD controller on SO(3)
//   Docs/03_SpringDamperTheory.md  -- spring/damping ratios, why damping scales as sqrt(spring)
//   Docs/04_ContactBackoffMath.md  -- the decaying impulse accumulator
//   Docs/05_SettleDetectionMath.md -- rolling-window settle detection
//   Docs/06_BlendingMath.md        -- physics blend weights and motor relaxation

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/EngineTypes.h"
#include "RagdollProComponent.generated.h"

class USkeletalMeshComponent;
class UPrimitiveComponent;
struct FConstraintInstance;
struct FHitResult;

UENUM(BlueprintType)
enum class ERagdollProState : uint8
{
	Inactive,
	BlendingIn,
	Active,
	Settling,
	BlendingOut,
	Done
};

// One entry = one bone chain override. Exact-match wins over inherited; nearest ancestor wins
// among inherited. Bones with no matching profile use multiplier 1.
USTRUCT(BlueprintType)
struct FRagdollProBoneProfile
{
	GENERATED_BODY()

	// Joint name as it appears in the Physics Asset (= child bone of the constraint).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RagdollPro")
	FName BoneName;

	// If true this profile also applies to every descendant joint (until a closer profile
	// overrides it). If false it applies to this one joint only.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RagdollPro")
	bool bIncludeChildren = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RagdollPro", meta = (ClampMin = "0.0"))
	float SpringMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RagdollPro", meta = (ClampMin = "0.0"))
	float DampingMultiplier = 1.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRagdollProStateChanged, ERagdollProState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRagdollProSettled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRagdollProFinished);

UCLASS(ClassGroup = (Physics), meta = (BlueprintSpawnableComponent, DisplayName = "RagdollPro Component"))
class RAGDOLLPRO_API URagdollProComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URagdollProComponent();

	// Full ragdoll. StartBone is usually "pelvis"; NAME_None simulates every body in the asset.
	// Callable from any state -- escalating a hit reaction into a full ragdoll re-initializes.
	UFUNCTION(BlueprintCallable, Category = "RagdollPro")
	void StartRagdoll(FName StartBone = NAME_None);

	// Partial-body powered reaction: simulates only the branch below HitBone with boosted motors,
	// applies the impulse, and auto-blends back after Duration (<= 0 uses HitReactionDuration).
	// Ignored while a full ragdoll owns the body.
	UFUNCTION(BlueprintCallable, Category = "RagdollPro")
	void StartHitReaction(FName HitBone, FVector Impulse, float Duration = -1.f);

	// Immediately snaps back to full animation control (respawn, cleanup, pickup, etc).
	// Does NOT broadcast OnFinished -- only the natural blend-out does.
	UFUNCTION(BlueprintCallable, Category = "RagdollPro")
	void CancelRagdoll();

	// Stops streaming motor targets from animation; drives keep pulling toward the last targets
	// they saw (e.g. call at the moment of death to make the body strain toward its final pose).
	UFUNCTION(BlueprintCallable, Category = "RagdollPro")
	void FreezeMotorTargets();

	// Resumes streaming targets from the live animation pose (no-op if
	// bDriveTargetsFromAnimation is false).
	UFUNCTION(BlueprintCallable, Category = "RagdollPro")
	void UnfreezeMotorTargets();

	// Gameplay-driven master strength (death fade, stun, etc). Multiplies everything else.
	UFUNCTION(BlueprintCallable, Category = "RagdollPro")
	void SetGlobalMotorScale(float Scale);

	// Impulse to a bone, propagated up the parent chain with falloff so a shot to the hand also
	// nudges the forearm/upper arm instead of hinging unnaturally at one joint.
	UFUNCTION(BlueprintCallable, Category = "RagdollPro")
	void ApplyImpulseToBone(FName Bone, FVector Impulse, bool bVelChange = true, int32 PropagateDepth = 1, float PropagateFalloff = 0.4f);

	// Getup helpers -----------------------------------------------------------------------------

	// True if the pelvis chest-axis points into the ground. Pick your getup animation off this.
	// Verify PelvisLocalChestAxis against YOUR skeleton first -- it is not universal.
	UFUNCTION(BlueprintPure, Category = "RagdollPro")
	bool IsLyingFaceDown() const;

	UFUNCTION(BlueprintPure, Category = "RagdollPro")
	FVector GetPelvisWorldLocation() const;

	UFUNCTION(BlueprintPure, Category = "RagdollPro")
	ERagdollProState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "RagdollPro")
	bool IsHitReactionActive() const { return bHitReaction && State != ERagdollProState::Inactive && State != ERagdollProState::Done; }

	// Final composed strength multiplier last pushed to the joints (for debugging/UI).
	UFUNCTION(BlueprintPure, Category = "RagdollPro")
	float GetCurrentMotorScale() const { return FMath::Max(LastAppliedScale, 0.f); }

	UPROPERTY(BlueprintAssignable, Category = "RagdollPro")
	FOnRagdollProStateChanged OnStateChanged;

	// Fires once, right before the blend-out ramp starts. Bind your pose-snapshot logic here.
	// Not fired for hit reactions (animation never stopped, there is nothing to snapshot).
	UPROPERTY(BlueprintAssignable, Category = "RagdollPro")
	FOnRagdollProSettled OnSettled;

	// Fires once physics blend weight has fully returned to 0 after a natural blend-out.
	UPROPERTY(BlueprintAssignable, Category = "RagdollPro")
	FOnRagdollProFinished OnFinished;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---------------- Setup ----------------

	// Bones sampled each tick to judge whether the ragdoll has settled. Keep this short --
	// extremities (hands, feet, head) show residual jitter first and are the cheapest tell.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Setup")
	TArray<FName> KeyBones = { TEXT("pelvis"), TEXT("head"), TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r") };

	// Forces projection on for every driven constraint at ragdoll start. Safety net only -- the
	// authoritative place is per constraint in the Physics Asset Editor.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Setup")
	bool bAutoEnableConstraintProjection = true;

	// While ragdolled, forces VisibilityBasedAnimTickOption to AlwaysTickPoseAndRefreshBones so
	// the anim pose (= motor targets) keeps evaluating even off-screen, then restores the old
	// setting when done. Disable if you manage anim tick options yourself.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Setup")
	bool bManageAnimTickOption = true;

	// ---------------- Motors ----------------

	// If true (default), constraint drive targets are refreshed from the animated local-space
	// pose every frame (engine bUpdateJointsFromAnimation path) -- the ragdoll "performs" whatever
	// the AnimBP outputs. If false, targets stay wherever they were last set (reference pose if
	// never set), giving a uniformly stiff corpse.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Motors")
	bool bDriveTargetsFromAnimation = true;

	// Base SLERP drive spring (orientation strength) before any multipliers. Tune by eye: too low
	// and the body is soup, too high and it fights gravity like a mannequin. Start ~500-2000.
	// See Docs/03_SpringDamperTheory.md for the physical meaning of this number.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Motors", meta = (ClampMin = "0.0"))
	float BaseAngularSpring = 1000.f;

	// Base SLERP drive damping. Also acts as joint friction (the velocity drive targets zero
	// angular velocity), which is what kills the endless micro-wobble at rest.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Motors", meta = (ClampMin = "0.0"))
	float BaseAngularDamping = 80.f;

	// Per-joint torque budget. 0 = unclamped. A finite budget (try 30000-100000) is what makes
	// heavy impacts visibly overpower the pose instead of the pose winning every argument.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Motors", meta = (ClampMin = "0.0"))
	float MaxAngularForce = 0.f;

	// Chain overrides: e.g. spine/neck at 1.5-2x, arms at 0.5x, hands at 0.2x.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Motors")
	TArray<FRagdollProBoneProfile> BoneProfiles;

	// Optional: X = seconds since StartRagdoll, Y = strength multiplier. Author it descending to
	// 0 to make the body go progressively limp (classic "life leaving the body"). Unset = 1.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Motors")
	TObjectPtr<UCurveFloat> StrengthOverLifetimeCurve = nullptr;

	// ---------------- Blend curves (X = 0..1 normalized time, Y = physics blend weight 0..1) ----------------

	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Curves")
	TObjectPtr<UCurveFloat> BlendInCurve = nullptr;

	// Author this descending (Y goes 1 -> 0) since it's applied directly as the blend weight.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Curves")
	TObjectPtr<UCurveFloat> BlendOutCurve = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Timing")
	float BlendInDuration = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Timing")
	float BlendOutDuration = 0.5f;

	// ---------------- Contact detection ----------------

	// Multiplier applied to motor strength while in "complex contact". 0 = fully passive ragdoll
	// (let the collision solver alone resolve it), 1 = no reduction at all.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Contact", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ContactDriveMultiplier = 0.1f;

	// How fast the live multiplier chases its target, per second.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Contact")
	float DriveMultiplierInterpSpeed = 6.f;

	// Decayed cumulative impulse magnitude above which we consider the ragdoll "in complex
	// contact". Tune by logging ContactAccumulator in a clean landing vs. a stack/pileup.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Contact")
	float ContactImpulseThreshold = 300.f;

	// Exponential decay rate (per second) applied to the impulse accumulator every tick.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Contact")
	float ContactImpulseDecayRate = 4.f;

	// ---------------- Settle detection ----------------

	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Settle")
	float SettleLinearVelocityThreshold = 40.f; // cm/s

	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Settle")
	float SettleAngularVelocityThreshold = 60.f; // deg/s

	// How long the rolling-average speed must stay under threshold before we call it settled.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Settle")
	float SettleSustainDuration = 0.25f;

	// Rolling window size in samples (~ticks). Smooths against contact chatter.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Settle")
	int32 SettleWindowSize = 12;

	// Hard ceiling: force the blend-out this many seconds after entering Active even if never
	// "settled" -- saves the corner-stuck case that would otherwise wobble forever.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|Settle")
	float StuckTimeout = 4.f;

	// ---------------- Hit reactions ----------------

	// Default hold time in Active before auto blend-out (per-call Duration overrides).
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|HitReaction")
	float HitReactionDuration = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|HitReaction")
	float HitReactionBlendInDuration = 0.08f;

	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|HitReaction")
	float HitReactionBlendOutDuration = 0.35f;

	// Motors run this much stronger during hit reactions so the branch snaps back toward the
	// animation instead of dangling.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|HitReaction", meta = (ClampMin = "0.0"))
	float HitReactionStrengthMultiplier = 1.75f;

	// ---------------- Owner follow / getup ----------------

	// While Active (full ragdoll only), moves the owning actor's root to hover over the pelvis so
	// the camera follows the body and the capsule is already in place for getup. Assumes the mesh
	// is NOT the root component. Invisible at blend weight 1 (simulated bones live in world space).
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|OwnerFollow")
	bool bFollowPelvisWithOwner = false;

	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|OwnerFollow")
	FName PelvisBoneName = TEXT("pelvis");

	// Height of the root above the traced floor (typically your capsule half height).
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|OwnerFollow")
	float OwnerFollowZOffset = 88.f;

	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|OwnerFollow")
	float OwnerFollowTraceDistance = 120.f;

	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|OwnerFollow")
	TEnumAsByte<ECollisionChannel> OwnerFollowTraceChannel = ECC_Visibility;

	// Local-space axis of the pelvis bone that points out of the character's chest, used by
	// IsLyingFaceDown(). Skeleton-dependent: for the UE mannequin family it is NOT +Z -- inspect
	// your pelvis bone axes in the Skeleton editor and set this accordingly.
	UPROPERTY(EditDefaultsOnly, Category = "RagdollPro|OwnerFollow")
	FVector PelvisLocalChestAxis = FVector(0.f, 0.f, 1.f);

	// ---------------- Debug ----------------

	// Draws state/strength text at the pelvis and settle spheres on KeyBones (dev builds only).
	UPROPERTY(EditAnywhere, Category = "RagdollPro|Debug")
	bool bDrawDebug = false;

private:
	struct FMotorEntry
	{
		FConstraintInstance* Constraint = nullptr; // valid while physics state is alive; table is rebuilt every Start* and cleared on finish
		float SpringMul = 1.f;
		float DampingMul = 1.f;
	};

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> MeshComp = nullptr;

	ERagdollProState State = ERagdollProState::Inactive;
	FName ActiveStartBone = NAME_None;
	bool bHitReaction = false;
	float ActiveHitReactionDuration = 0.f;

	TArray<FMotorEntry> MotorTable;

	float StateTime = 0.f;
	float RagdollTime = 0.f; // seconds since StartRagdoll/StartHitReaction, across all states
	float ContactAccumulator = 0.f;
	float CurrentContactMultiplier = 1.f;
	float GlobalMotorScale = 1.f;
	float LastAppliedScale = -1.f;
	float CurrentBlendWeight = 0.f;
	float SettledForDuration = 0.f;
	uint8 SavedAnimTickOption = 0;

	// Fixed-size ring buffer -- no per-tick heap churn.
	TArray<float> VelocitySamples;
	int32 VelocitySampleIndex = 0;
	int32 VelocitySampleCount = 0;

	UFUNCTION()
	void HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	void SetState(ERagdollProState NewState);
	void ResetRuntimeState();
	void PrepareMeshForSimulation();
	void RestoreAnimSettings();
	void FinishRagdoll(bool bBroadcastFinished);

	void BuildMotorTable();
	void ResolveBoneMultipliers(FName Bone, float& OutSpringMul, float& OutDampingMul) const;
	void ConfigureConstraintProjection();

	float ComposeGlobalScale() const;
	void ApplyMotorStrength(float Scale, bool bForce = false);
	void StopAllMotors();
	void UpdateContactMultiplier(float DeltaTime);

	void SetBlendWeightBelow(float Weight);

	float SampleMaxKeyBoneSpeed() const;
	void PushVelocitySample(float Value);
	float GetRollingAverageVelocity() const;

	void UpdateOwnerFollow();
	void DrawDebugInfo() const;

	float GetActiveBlendInDuration() const { return bHitReaction ? HitReactionBlendInDuration : BlendInDuration; }
	float GetActiveBlendOutDuration() const { return bHitReaction ? HitReactionBlendOutDuration : BlendOutDuration; }

	void TickBlendingIn(float DeltaTime);
	void TickActive(float DeltaTime);
	void TickBlendingOut(float DeltaTime);
};
