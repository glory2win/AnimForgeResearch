// JacobianDLSSolverTests.cpp
//
// Automation tests for the raw DLS solver (no skeleton assets required, so
// these run headless: -ExecCmds="Automation RunTests AnimForge.JacobianDLSIK").
// What each test proves, and why it matters, is documented in TESTING.md.
//
// Adjust the include path to wherever JacobianDLSSolver.h lives in your module.

#include "Misc/AutomationTest.h"
#include "JacobianDLSSolver.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace JacobianDLSTest
{
	/** Straight chain of NumJoints along +X, each bone BoneLength long. Reach = (NumJoints-1)*BoneLength. */
	TArray<FDLSJoint> BuildStraightChain(int32 NumJoints, double BoneLength)
	{
		TArray<FDLSJoint> Joints;
		Joints.SetNum(NumJoints);
		for (int32 i = 1; i < NumJoints; ++i)
		{
			Joints[i].LocalOffset = FVector(BoneLength, 0, 0);
		}
		return Joints;
	}

	/** Bend joint Index by AngleDegrees around local Z. */
	void Bend(TArray<FDLSJoint>& Joints, int32 Index, double AngleDegrees)
	{
		Joints[Index].LocalRotation = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(AngleDegrees)) * Joints[Index].LocalRotation;
	}

	FJacobianDLSSettings DefaultSettings()
	{
		FJacobianDLSSettings S;
		S.MaxIterations = 32;
		return S;
	}

	bool ChainIsFinite(const TArray<FDLSJoint>& Joints)
	{
		for (const FDLSJoint& J : Joints)
		{
			if (J.ComponentPosition.ContainsNaN() || J.LocalRotation.ContainsNaN() || !J.LocalRotation.IsNormalized())
			{
				return false;
			}
		}
		return true;
	}
}

// ---------------------------------------------------------------------------
// 1. The analytic Jacobian column a x (p_e - p_i) must match a finite-difference
//    probe of the forward kinematics. This validates the single most important
//    equation in the whole system: if this holds, the solver is descending the
//    true gradient; if it drifts, every other behavior is luck.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJacobianDLSIK_FiniteDifferenceJacobian,
	"AnimForge.JacobianDLSIK.FiniteDifferenceJacobian",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJacobianDLSIK_FiniteDifferenceJacobian::RunTest(const FString& Parameters)
{
	using namespace JacobianDLSTest;

	TArray<FDLSJoint> Joints = BuildStraightChain(3, 30.0);
	Bend(Joints, 0, 20.0);
	Bend(Joints, 1, -35.0);
	const FTransform Base = FTransform::Identity;
	FJacobianDLSSolver::ForwardKinematics(Joints, Base);
	const FVector Effector = Joints.Last().ComponentPosition;

	const double Eps = 1.e-4;
	const FVector Axes[3] = { FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector };

	for (int32 JointIndex = 0; JointIndex < Joints.Num() - 1; ++JointIndex)
	{
		for (const FVector& Axis : Axes)
		{
			// Analytic column: a x (p_e - p_i)
			const FVector Analytic = FVector::CrossProduct(Axis, Effector - Joints[JointIndex].ComponentPosition);

			// Finite difference: premultiply the joint's component-space rotation
			// by exp(Eps * a) and measure how far the effector moved.
			TArray<FDLSJoint> Perturbed = Joints;
			const FQuat ParentRot = (JointIndex > 0) ? Joints[JointIndex - 1].ComponentRotation : Base.GetRotation();
			const FQuat DeltaLocal = ParentRot.Inverse() * (FQuat(Axis, Eps) * ParentRot);
			Perturbed[JointIndex].LocalRotation = DeltaLocal * Perturbed[JointIndex].LocalRotation;
			FJacobianDLSSolver::ForwardKinematics(Perturbed, Base);
			const FVector FiniteDiff = (Perturbed.Last().ComponentPosition - Effector) / Eps;

			TestTrue(FString::Printf(TEXT("Joint %d axis (%g,%g,%g): analytic vs FD column"),
					JointIndex, Axis.X, Axis.Y, Axis.Z),
				FVector::Dist(Analytic, FiniteDiff) < 0.05);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// 2. Reachable target inside the workspace -> converges to within tolerance.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJacobianDLSIK_ReachableTargetConverges,
	"AnimForge.JacobianDLSIK.ReachableTargetConverges",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJacobianDLSIK_ReachableTargetConverges::RunTest(const FString& Parameters)
{
	using namespace JacobianDLSTest;

	TArray<FDLSJoint> Joints = BuildStraightChain(3, 30.0); // reach 60
	Bend(Joints, 0, 10.0); // start slightly off-singular, as a real animated pose would
	const FVector Target(35.0, 25.0, 10.0); // |Target| ~ 44, comfortably inside

	FJacobianDLSDebugInfo Debug;
	FJacobianDLSSolver::Solve(Joints, FTransform::Identity, Target, DefaultSettings(), &Debug);

	TestTrue(TEXT("Chain state is finite and normalized"), ChainIsFinite(Joints));
	TestTrue(FString::Printf(TEXT("Converged: error %.3f cm after %d iterations"), Debug.FinalError, Debug.IterationsUsed),
		Debug.FinalError < 0.15);
	return true;
}

// ---------------------------------------------------------------------------
// 3. THE DLS claim: near a singular pose (almost-straight chain, target back
//    along the chain axis), an (effectively) undamped pseudoinverse step is huge
//    — the 1/sigma blow-up — while the damped step stays bounded. This is the
//    reason this solver exists; see THEORY.md sections 3.5-3.6.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJacobianDLSIK_DampingBoundsSingularStep,
	"AnimForge.JacobianDLSIK.DampingBoundsSingularStep",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJacobianDLSIK_DampingBoundsSingularStep::RunTest(const FString& Parameters)
{
	using namespace JacobianDLSTest;

	const FVector Target(30.0, 0.0, 0.0); // on the chain axis, half reach: singular direction

	auto MaxStepAfterOneIteration = [&](const FJacobianDLSSettings& S) -> double
	{
		TArray<FDLSJoint> Joints = BuildStraightChain(3, 30.0);
		Bend(Joints, 1, 2.0); // *near*-singular: sigma_min small but nonzero -> worst case
		TArray<FDLSJoint> Before = Joints;
		FJacobianDLSSettings OneIter = S;
		OneIter.MaxIterations = 1;
		FJacobianDLSSolver::Solve(Joints, FTransform::Identity, Target, OneIter, nullptr);

		double MaxStep = 0.0;
		for (int32 i = 0; i < Joints.Num(); ++i)
		{
			MaxStep = FMath::Max(MaxStep, (double)Before[i].LocalRotation.AngularDistance(Joints[i].LocalRotation));
		}
		return MaxStep;
	};

	FJacobianDLSSettings Undamped = DefaultSettings();
	Undamped.Damping = 0.001;
	Undamped.MaxExtraDamping = 0.0;
	Undamped.MaxAngleStepRadians = 100.0; // disable the safety clamp to expose the raw step
	Undamped.MaxErrorStep = 1000.0;

	FJacobianDLSSettings Damped = DefaultSettings();
	Damped.MaxAngleStepRadians = 100.0;   // same clamp off: bounding must come from lambda alone
	Damped.MaxErrorStep = 1000.0;

	const double UndampedStep = MaxStepAfterOneIteration(Undamped);
	const double DampedStep = MaxStepAfterOneIteration(Damped);

	AddInfo(FString::Printf(TEXT("Max joint step near singularity: undamped %.2f rad, damped %.4f rad"), UndampedStep, DampedStep));
	TestTrue(TEXT("Undamped pseudoinverse step explodes near the singularity (> 1 rad)"), UndampedStep > 1.0);
	TestTrue(TEXT("Damped step stays bounded (< 0.35 rad)"), DampedStep < 0.35);
	return true;
}

// ---------------------------------------------------------------------------
// 4. Unreachable target: the chain must extend toward it and settle at the
//    workspace boundary without oscillating or producing NaNs. (Plain Jacobian
//    methods without error clamping ripple violently here.)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJacobianDLSIK_UnreachableTargetStable,
	"AnimForge.JacobianDLSIK.UnreachableTargetStable",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJacobianDLSIK_UnreachableTargetStable::RunTest(const FString& Parameters)
{
	using namespace JacobianDLSTest;

	TArray<FDLSJoint> Joints = BuildStraightChain(3, 30.0); // reach 60
	Bend(Joints, 0, 25.0);
	Bend(Joints, 1, 20.0);
	const FVector Target(200.0, 50.0, 0.0); // distance ~206, far outside

	FJacobianDLSSettings Settings = DefaultSettings();
	Settings.MaxIterations = 48;
	FJacobianDLSDebugInfo Debug;
	FJacobianDLSSolver::Solve(Joints, FTransform::Identity, Target, Settings, &Debug);

	TestTrue(TEXT("Chain state is finite and normalized"), ChainIsFinite(Joints));

	const double Distance = Target.Size();
	const double ExpectedResidual = Distance - 60.0;
	TestTrue(FString::Printf(TEXT("Residual %.2f ~ boundary distance %.2f"), Debug.FinalError, ExpectedResidual),
		FMath::Abs(Debug.FinalError - ExpectedResidual) < 2.0);

	const FVector ChainDir = Joints.Last().ComponentPosition.GetSafeNormal();
	const FVector TargetDir = Target.GetSafeNormal();
	TestTrue(TEXT("Chain points at the target"), (ChainDir | TargetDir) > 0.995);
	return true;
}

// ---------------------------------------------------------------------------
// 5. Swing/twist limits are hard constraints: after any solve, no limited
//    joint may exceed its cone or roll budget.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJacobianDLSIK_JointLimitsRespected,
	"AnimForge.JacobianDLSIK.JointLimitsRespected",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJacobianDLSIK_JointLimitsRespected::RunTest(const FString& Parameters)
{
	using namespace JacobianDLSTest;

	TArray<FDLSJoint> Joints = BuildStraightChain(4, 25.0);
	const double SwingLimitDeg = 15.0;
	const double TwistLimitDeg = 5.0;
	for (int32 i = 0; i < Joints.Num(); ++i)
	{
		Joints[i].bUseLimits = true;
		Joints[i].SwingLimitRadians = FMath::DegreesToRadians(SwingLimitDeg);
		Joints[i].TwistLimitRadians = FMath::DegreesToRadians(TwistLimitDeg);
	}

	// Target demanding far more bend than the limits allow.
	const FVector Target(-20.0, 40.0, 30.0);
	FJacobianDLSSolver::Solve(Joints, FTransform::Identity, Target, DefaultSettings(), nullptr);

	for (int32 i = 0; i < Joints.Num(); ++i)
	{
		const FQuat Rel = Joints[i].RefLocalRotation.Inverse() * Joints[i].LocalRotation;
		FQuat Swing, Twist;
		Rel.ToSwingTwist(FVector::XAxisVector, Swing, Twist);
		if (Swing.W < 0) { Swing = FQuat(-Swing.X, -Swing.Y, -Swing.Z, -Swing.W); }

		const double SwingDeg = FMath::RadiansToDegrees(2.0 * FMath::Atan2(FVector(Swing.X, Swing.Y, Swing.Z).Size(), Swing.W));
		const double TwistDeg = FMath::RadiansToDegrees(FMath::Abs(FMath::UnwindRadians(
			2.0 * FMath::Atan2(FVector(Twist.X, Twist.Y, Twist.Z) | FVector::XAxisVector, Twist.W))));

		TestTrue(FString::Printf(TEXT("Joint %d swing %.2f <= %.2f deg"), i, SwingDeg, SwingLimitDeg), SwingDeg <= SwingLimitDeg + 0.5);
		TestTrue(FString::Printf(TEXT("Joint %d twist %.2f <= %.2f deg"), i, TwistDeg, TwistLimitDeg), TwistDeg <= TwistLimitDeg + 0.5);
	}
	return true;
}

// ---------------------------------------------------------------------------
// 6. Weight = 0 locks a joint completely (its animated local rotation survives).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJacobianDLSIK_LockedJointDoesNotMove,
	"AnimForge.JacobianDLSIK.LockedJointDoesNotMove",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJacobianDLSIK_LockedJointDoesNotMove::RunTest(const FString& Parameters)
{
	using namespace JacobianDLSTest;

	TArray<FDLSJoint> Joints = BuildStraightChain(4, 25.0);
	Bend(Joints, 0, 15.0);
	Joints[0].Weight = 0.0f;
	const FQuat LockedRotation = Joints[0].LocalRotation;

	FJacobianDLSSolver::Solve(Joints, FTransform::Identity, FVector(30, 30, 10), DefaultSettings(), nullptr);

	TestTrue(TEXT("Locked joint's local rotation unchanged"),
		LockedRotation.AngularDistance(Joints[0].LocalRotation) < 1.e-6);
	return true;
}

// ---------------------------------------------------------------------------
// 7. Determinism: identical inputs -> bit-identical iteration path. Required
//    for anim worker threads, replays, and network sync.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJacobianDLSIK_Deterministic,
	"AnimForge.JacobianDLSIK.Deterministic",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJacobianDLSIK_Deterministic::RunTest(const FString& Parameters)
{
	using namespace JacobianDLSTest;

	auto Run = [&]() -> TArray<FDLSJoint>
	{
		TArray<FDLSJoint> Joints = BuildStraightChain(5, 20.0);
		Bend(Joints, 1, 12.0);
		Bend(Joints, 3, -8.0);
		FJacobianDLSSolver::Solve(Joints, FTransform::Identity, FVector(40, 30, -15), DefaultSettings(), nullptr);
		return Joints;
	};

	const TArray<FDLSJoint> A = Run();
	const TArray<FDLSJoint> B = Run();
	for (int32 i = 0; i < A.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Joint %d position identical"), i),
			FVector::Dist(A[i].ComponentPosition, B[i].ComponentPosition) < 1.e-12);
	}
	return true;
}

// ---------------------------------------------------------------------------
// 8. Performance smoke test: 30-joint chain, full iteration budget, 1000 solves.
//    The per-iteration cost is O(N) + one 3x3 Cholesky, so this should be far
//    under budget; the assert is deliberately loose to stay CI-stable, the
//    logged number is what you actually track. See DESIGN.md section 5.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJacobianDLSIK_LongChainPerformance,
	"AnimForge.JacobianDLSIK.LongChainPerformance",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FJacobianDLSIK_LongChainPerformance::RunTest(const FString& Parameters)
{
	using namespace JacobianDLSTest;

	FJacobianDLSSettings Settings = DefaultSettings();
	Settings.MaxIterations = 12;
	Settings.Tolerance = 0.001; // unreachable tolerance -> forces all 12 iterations

	TArray<FDLSJoint> Template = BuildStraightChain(30, 10.0);
	Bend(Template, 1, 5.0);
	const FVector Target(500.0, 100.0, 0.0); // unreachable: worst case, never early-outs

	constexpr int32 NumSolves = 1000;
	TArray<FDLSJoint> Joints;
	const double Start = FPlatformTime::Seconds();
	for (int32 s = 0; s < NumSolves; ++s)
	{
		Joints = Template;
		FJacobianDLSSolver::Solve(Joints, FTransform::Identity, Target, Settings, nullptr);
	}
	const double TotalMs = (FPlatformTime::Seconds() - Start) * 1000.0;

	AddInfo(FString::Printf(TEXT("30-joint chain, 12 iterations: %.1f us per solve (%d solves in %.2f ms)"),
		TotalMs * 1000.0 / NumSolves, NumSolves, TotalMs));
	TestTrue(TEXT("1000 worst-case solves complete within 250 ms"), TotalMs < 250.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
