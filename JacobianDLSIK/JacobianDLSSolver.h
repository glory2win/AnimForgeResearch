// JacobianDLSSolver.h
//
// Core Jacobian Damped Least Squares (DLS) IK solver.
// Deliberately engine-agnostic apart from UE math types: no anim graph, skeleton
// or UObject dependencies, so the math can be unit-tested in isolation
// (see Tests/JacobianDLSSolverTests.cpp) and reused outside the anim node
// (Control Rig units, editor tools, offline retarget passes).
//
// The algorithm and every identity used here are derived step by step in
// THEORY.md. Implementation decisions are documented in DESIGN.md.
//
// Key property: per iteration the solver is O(N) in chain length with a single
// 3x3 linear solve — there is no NxN or 3Nx3N matrix anywhere. This follows
// from the JJt formulation of DLS plus the identity
//     sum over orthonormal axes a of (a x r)(a x r)^T  =  |r|^2 I - r r^T
// (THEORY.md section 7).

#pragma once

#include "CoreMinimal.h"

/**
 * One joint of the IK chain, ordered root -> tip.
 * Each joint is a full 3-DOF ball joint (three revolute DOF around the
 * component-space orthonormal axes) — matching how skeletal animation bones
 * actually behave. LocalOffset/LocalRotation are relative to the previous
 * joint in the chain (or to the Base transform for joint 0).
 */
struct YOURGAME_API FDLSJoint
{
	// --- fixed per solve: chain description ---

	/** Translation from the parent joint, in parent space. Encodes bone length. */
	FVector LocalOffset = FVector::ZeroVector;

	/** Reference-pose local rotation. Center of the swing/twist limit cone. */
	FQuat RefLocalRotation = FQuat::Identity;

	/**
	 * Per-joint weight in [0,1]. 0 = locked, 1 = fully free.
	 * Mathematically this is the diagonal of W in dTheta = W J^T (J W J^T + L^2 I)^-1 e,
	 * i.e. an inverse stiffness: lower weight -> the least-squares solution
	 * routes less of the motion through this joint (THEORY.md section 9).
	 */
	float Weight = 1.0f;

	// --- joint limits (swing/twist decomposition against RefLocalRotation) ---

	bool bUseLimits = false;

	/** Bone-local twist axis: the bone's primary (length) axis. X on UE mannequins. Must be normalized. */
	FVector TwistAxis = FVector::XAxisVector;

	float SwingLimitRadians = PI;
	float TwistLimitRadians = PI;

	// --- state: input pose in, solved pose out ---

	/** Parent-space rotation. This is what the solver actually changes. */
	FQuat LocalRotation = FQuat::Identity;

	/** FK results, component space. Valid after Solve()/ForwardKinematics(). */
	FVector ComponentPosition = FVector::ZeroVector;
	FQuat ComponentRotation = FQuat::Identity;
};

/** Solver tuning. All length units are whatever the chain uses (cm in UE). */
struct YOURGAME_API FJacobianDLSSettings
{
	/** Upper bound on Newton-style iterations per solve. Typical convergence: 3-8. */
	int32 MaxIterations = 12;

	/** Stop when the effector is within this distance of the target (cm). */
	double Tolerance = 0.1;

	/**
	 * Base damping lambda, in length units (cm). This is THE knob of DLS:
	 * the per-mode gain is sigma/(sigma^2 + lambda^2) instead of 1/sigma, so the
	 * response to error along a near-singular direction is bounded by 1/(2*lambda)
	 * instead of exploding. Rule of thumb: 1-5% of chain length. See THEORY.md section 6.
	 */
	double Damping = 1.0;

	/**
	 * Extra lambda blended in as the chain approaches a singular configuration,
	 * detected via the isotropy of J W J^T (Nakamura-style adaptive damping,
	 * THEORY.md section 8). 0 disables adaptation.
	 */
	double MaxExtraDamping = 20.0;

	/**
	 * Isotropy = det(M) / (trace(M)/3)^3 of M = J W J^T, a dimensionless
	 * manipulability measure in [0,1]: 1 = perfectly isotropic (can move the
	 * effector equally in all directions), 0 = singular. Below this threshold
	 * the extra damping ramps in quadratically.
	 */
	double IsotropyThreshold = 0.1;

	/**
	 * Clamp on the error vector magnitude fed to each iteration (cm).
	 * The Jacobian is a *linearization*; chasing a 3-meter error in one linear
	 * step is meaningless. Clamped-error DLS keeps steps inside the region
	 * where the linear model is honest (Buss & Kim). ~ half chain length.
	 */
	double MaxErrorStep = 30.0;

	/** Per-joint, per-iteration rotation clamp (radians). Safety net against overshoot. */
	double MaxAngleStepRadians = 0.174533; // 10 degrees
};

/** Per-solve diagnostics, cheap enough to always fill. */
struct YOURGAME_API FJacobianDLSDebugInfo
{
	int32 IterationsUsed = 0;
	double FinalError = 0.0;
	/** Total lambda used on the last iteration (base + adaptive). */
	double LastLambda = 0.0;
	/** Isotropy of J W J^T on the last iteration. Near 0 = solving at a singularity. */
	double LastIsotropy = 1.0;
};

class YOURGAME_API FJacobianDLSSolver
{
public:
	/**
	 * Recompute component-space positions/rotations from local offsets/rotations.
	 * Base is the transform of the chain root's *parent* (identity if none), so
	 * LocalRotation values stay true skeletal local rotations.
	 */
	static void ForwardKinematics(TArrayView<FDLSJoint> Joints, const FTransform& Base);

	/**
	 * Move the last joint's origin (the end effector) toward TargetCS.
	 * Joints must be ordered root -> tip; the tip joint's rotation is never
	 * modified (rotating the tip cannot move its own origin — its Jacobian
	 * columns are identically zero), so incoming tip orientation is preserved.
	 *
	 * Returns the number of iterations executed. On return, joint local
	 * rotations are updated and FK state is valid.
	 */
	static int32 Solve(TArrayView<FDLSJoint> Joints, const FTransform& Base, const FVector& TargetCS,
		const FJacobianDLSSettings& Settings, FJacobianDLSDebugInfo* OutDebug = nullptr);

private:
	/**
	 * Solve (M + Lambda2 * I) y = B for y. M is symmetric positive semi-definite
	 * by construction, so with Lambda2 > 0 the system is symmetric positive
	 * definite and an unpivoted 3x3 Cholesky factorization cannot fail.
	 */
	static bool SolveDamped3x3(const double M[3][3], double Lambda2, const FVector& B, FVector& OutY);

	/** Clamp LocalRotation against RefLocalRotation using swing/twist decomposition. */
	static void ClampSwingTwist(FDLSJoint& Joint);
};
