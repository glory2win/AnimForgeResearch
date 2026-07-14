// AnimNode_JacobianDLSIK.h
//
// Skeletal control node: solves a bone chain (RootBone -> TipBone) so the tip
// bone's origin reaches EffectorLocation, using the damped-least-squares solver
// in JacobianDLSSolver.h/.cpp. Runtime module file (needs AnimGraphRuntime).
//
// Compared to the engine's chain solvers:
//  - CCD: greedy joint-at-a-time, curls the chain tip-heavy, order dependent.
//  - FABRIK: solves positions then back-derives rotations — twist is undefined
//    and limits are reprojection hacks.
//  - This node: one least-squares problem over all joints per iteration, exact
//    per-joint weighting, and bounded response at singular poses (straight-arm
//    reach, planted leg) via damping. See THEORY.md sections 3.5 and 3.10.

#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "JacobianDLSSolver.h"
#include "AnimNode_JacobianDLSIK.generated.h"

/** Per-bone solver overrides. Bones without an entry use Weight = 1, no limits. */
USTRUCT()
struct YOURGAME_API FJacobianDLSJointSettings
{
	GENERATED_BODY()

	/** Bone this override applies to (must be inside the solved chain). */
	UPROPERTY(EditAnywhere, Category = Joint)
	FBoneReference Bone;

	/** 0 = locked, 1 = fully free. Scales how much of the solution this joint absorbs. */
	UPROPERTY(EditAnywhere, Category = Joint, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, Category = Joint)
	bool bUseLimits = false;

	/** Max tilt of the bone away from its reference-pose direction, degrees. */
	UPROPERTY(EditAnywhere, Category = Joint, meta = (EditCondition = "bUseLimits", ClampMin = "0.0", ClampMax = "180.0"))
	float SwingLimitDegrees = 60.0f;

	/** Max roll around the bone's own length axis, degrees. */
	UPROPERTY(EditAnywhere, Category = Joint, meta = (EditCondition = "bUseLimits", ClampMin = "0.0", ClampMax = "180.0"))
	float TwistLimitDegrees = 30.0f;

	/** Bone-local twist (length) axis. X for UE4/UE5 mannequin skeletons. */
	UPROPERTY(EditAnywhere, Category = Joint, meta = (EditCondition = "bUseLimits"))
	FVector TwistAxis = FVector::XAxisVector;
};

USTRUCT(BlueprintInternalUseOnly)
struct YOURGAME_API FAnimNode_JacobianDLSIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	/** First bone of the chain, closest to the skeleton root (e.g. upperarm_l). */
	UPROPERTY(EditAnywhere, Category = Chain)
	FBoneReference RootBone;

	/**
	 * Last bone of the chain — its origin is the end effector (e.g. hand_l).
	 * Its rotation is left untouched by the solver, preserving the animated
	 * orientation from the incoming pose.
	 */
	UPROPERTY(EditAnywhere, Category = Chain)
	FBoneReference TipBone;

	/** Target position for the tip bone, in EffectorLocationSpace. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Effector, meta = (PinShownByDefault))
	FVector EffectorLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Effector)
	TEnumAsByte<enum EBoneControlSpace> EffectorLocationSpace = BCS_ComponentSpace;

	/** Space reference bone — only used when EffectorLocationSpace is a bone space. */
	UPROPERTY(EditAnywhere, Category = Effector)
	FBoneReference EffectorTarget;

	UPROPERTY(EditAnywhere, Category = Solver, meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxIterations = 12;

	/** Stop iterating once the effector is within this many cm of the target. */
	UPROPERTY(EditAnywhere, Category = Solver, meta = (ClampMin = "0.001"))
	float Precision = 0.1f;

	/**
	 * Base damping lambda in cm. Bigger = smoother/more stable but slower to
	 * converge; smaller = snappier but livelier near singular poses.
	 * Rule of thumb: 1-5% of chain length (arm ~60cm -> 1..3).
	 */
	UPROPERTY(EditAnywhere, Category = Solver, meta = (ClampMin = "0.0"))
	float Damping = 1.0f;

	/** Extra lambda ramped in near singular configurations. 0 disables adaptive damping. */
	UPROPERTY(EditAnywhere, Category = Solver, meta = (ClampMin = "0.0"))
	float MaxExtraDamping = 20.0f;

	/** Isotropy of J W J^T below which extra damping ramps in. See solver header. */
	UPROPERTY(EditAnywhere, Category = Solver, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IsotropyThreshold = 0.1f;

	/** Error clamp per iteration (cm); keeps the linearization honest for far targets. ~half chain length. */
	UPROPERTY(EditAnywhere, Category = Solver, meta = (ClampMin = "0.1"))
	float MaxErrorStep = 30.0f;

	/** Per-joint, per-iteration rotation clamp in degrees. */
	UPROPERTY(EditAnywhere, Category = Solver, meta = (ClampMin = "0.1", ClampMax = "180.0"))
	float MaxAngleStepDegrees = 10.0f;

	/** Per-bone weight / limit overrides. */
	UPROPERTY(EditAnywhere, Category = Solver)
	TArray<FJacobianDLSJointSettings> JointSettings;

	// FAnimNode_SkeletalControlBase interface
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;

private:
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

	/** Chain compact-pose indices, root -> tip. Rebuilt whenever bones are (re)cached, e.g. on LOD change. */
	TArray<FCompactPoseBoneIndex> CachedChain;

	/** Reused across evaluations — no per-frame heap allocation. */
	TArray<FDLSJoint> ScratchJoints;

	/** Written on the anim worker thread, read by GatherDebugData (debug-only, tearing is acceptable). */
	FJacobianDLSDebugInfo LastDebugInfo;
};
