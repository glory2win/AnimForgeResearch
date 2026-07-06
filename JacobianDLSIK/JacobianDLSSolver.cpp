// JacobianDLSSolver.cpp
// See THEORY.md for the math, DESIGN.md for the implementation decisions.

#include "JacobianDLSSolver.h"

namespace JacobianDLS
{
	// Floor under square roots / divisors in the 3x3 factorization. The system is
	// SPD whenever Lambda2 > 0, so this only ever matters if a caller forces
	// Damping = 0 at an exact singularity.
	constexpr double FactorEpsilon = 1.e-12;

	FORCEINLINE double Det3(const double M[3][3])
	{
		return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
		     - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
		     + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
	}
}

void FJacobianDLSSolver::ForwardKinematics(TArrayView<FDLSJoint> Joints, const FTransform& Base)
{
	FVector ParentPos = Base.GetLocation();
	FQuat ParentRot = Base.GetRotation();
	for (FDLSJoint& J : Joints)
	{
		J.ComponentPosition = ParentPos + ParentRot.RotateVector(J.LocalOffset);
		J.ComponentRotation = ParentRot * J.LocalRotation;
		J.ComponentRotation.Normalize();
		ParentPos = J.ComponentPosition;
		ParentRot = J.ComponentRotation;
	}
}

bool FJacobianDLSSolver::SolveDamped3x3(const double M[3][3], const double Lambda2, const FVector& B, FVector& OutY)
{
	using JacobianDLS::FactorEpsilon;

	// A = M + Lambda2*I, factored as A = L*L^T (Cholesky).
	const double A00 = M[0][0] + Lambda2;
	const double A11 = M[1][1] + Lambda2;
	const double A22 = M[2][2] + Lambda2;
	const double A10 = M[1][0];
	const double A20 = M[2][0];
	const double A21 = M[2][1];

	const double L00 = FMath::Sqrt(FMath::Max(A00, FactorEpsilon));
	const double L10 = A10 / L00;
	const double L20 = A20 / L00;
	const double L11 = FMath::Sqrt(FMath::Max(A11 - L10 * L10, FactorEpsilon));
	const double L21 = (A21 - L20 * L10) / L11;
	const double L22 = FMath::Sqrt(FMath::Max(A22 - L20 * L20 - L21 * L21, FactorEpsilon));

	// Forward substitution: L z = B
	const double Z0 = B.X / L00;
	const double Z1 = (B.Y - L10 * Z0) / L11;
	const double Z2 = (B.Z - L20 * Z0 - L21 * Z1) / L22;

	// Back substitution: L^T y = z
	const double Y2 = Z2 / L22;
	const double Y1 = (Z1 - L21 * Y2) / L11;
	const double Y0 = (Z0 - L10 * Y1 - L20 * Y2) / L00;

	OutY = FVector(Y0, Y1, Y2);
	return !OutY.ContainsNaN();
}

void FJacobianDLSSolver::ClampSwingTwist(FDLSJoint& Joint)
{
	// Decompose the rotation *relative to the reference pose* as Rel = Swing * Twist,
	// where Twist is around the bone's own length axis. Swing tilts the bone
	// direction, Twist rolls around it — clamping them separately gives an
	// anatomically meaningful cone + roll limit.
	const FQuat Rel = Joint.RefLocalRotation.Inverse() * Joint.LocalRotation;
	FQuat Swing, Twist;
	Rel.ToSwingTwist(Joint.TwistAxis, Swing, Twist);

	// Force the shortest-arc representation (W >= 0) before reading angles.
	if (Swing.W < 0.0)
	{
		Swing = FQuat(-Swing.X, -Swing.Y, -Swing.Z, -Swing.W);
	}

	const double SwingAngle = 2.0 * FMath::Atan2(FVector(Swing.X, Swing.Y, Swing.Z).Size(), Swing.W); // [0, PI]
	if (SwingAngle > Joint.SwingLimitRadians)
	{
		Swing = FQuat::Slerp(FQuat::Identity, Swing, Joint.SwingLimitRadians / SwingAngle);
	}

	// Signed twist angle around TwistAxis: Twist = (sin(t/2)*axis, cos(t/2)),
	// where axis = +/- TwistAxis; the dot recovers the sign.
	const double TwistSin = FVector(Twist.X, Twist.Y, Twist.Z) | Joint.TwistAxis;
	double TwistAngle = FMath::UnwindRadians(2.0 * FMath::Atan2(TwistSin, Twist.W));
	TwistAngle = FMath::Clamp(TwistAngle, -Joint.TwistLimitRadians, Joint.TwistLimitRadians);
	Twist = FQuat(Joint.TwistAxis, TwistAngle);

	Joint.LocalRotation = Joint.RefLocalRotation * Swing * Twist;
	Joint.LocalRotation.Normalize();
}

int32 FJacobianDLSSolver::Solve(TArrayView<FDLSJoint> Joints, const FTransform& Base, const FVector& TargetCS,
	const FJacobianDLSSettings& Settings, FJacobianDLSDebugInfo* OutDebug)
{
	FJacobianDLSDebugInfo Debug;
	const int32 NumJoints = Joints.Num();
	if (NumJoints < 2)
	{
		if (OutDebug) { *OutDebug = Debug; }
		return 0;
	}

	ForwardKinematics(Joints, Base);

	int32 Iteration = 0;
	for (; Iteration < Settings.MaxIterations; ++Iteration)
	{
		const FVector Effector = Joints[NumJoints - 1].ComponentPosition;
		FVector Error = TargetCS - Effector;
		const double ErrorSize = Error.Size();
		if (ErrorSize <= Settings.Tolerance)
		{
			break;
		}

		// Clamped-error DLS: the Jacobian is a linearization, only trust it locally.
		if (ErrorSize > Settings.MaxErrorStep)
		{
			Error *= Settings.MaxErrorStep / ErrorSize;
		}

		// M = J W J^T = sum_i w_i * (|r_i|^2 I - r_i r_i^T),  r_i = effector - joint_i.
		// This is the whole 3x(3N) Jacobian collapsed to 3x3 in one O(N) pass
		// (THEORY.md section 7). The tip joint is skipped: r_tip = 0, it cannot
		// move its own origin.
		double M[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
		for (int32 i = 0; i < NumJoints - 1; ++i)
		{
			const double W = Joints[i].Weight;
			if (W <= 0.0)
			{
				continue;
			}
			const FVector R = Effector - Joints[i].ComponentPosition;
			const double RR = R | R;
			M[0][0] += W * (RR - R.X * R.X);
			M[1][1] += W * (RR - R.Y * R.Y);
			M[2][2] += W * (RR - R.Z * R.Z);
			M[0][1] -= W * R.X * R.Y;
			M[0][2] -= W * R.X * R.Z;
			M[1][2] -= W * R.Y * R.Z;
		}
		M[1][0] = M[0][1];
		M[2][0] = M[0][2];
		M[2][1] = M[1][2];

		// Adaptive damping: isotropy = det(M)/(trace(M)/3)^3 in [0,1] by AM-GM,
		// hits 0 exactly when M is singular. Ramp extra lambda in quadratically
		// below the threshold (Nakamura & Hanafusa style, THEORY.md section 8).
		double Lambda = Settings.Damping;
		{
			const double Trace = M[0][0] + M[1][1] + M[2][2];
			double Isotropy = 0.0;
			if (Trace > UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				const double Mean = Trace / 3.0;
				Isotropy = FMath::Clamp(JacobianDLS::Det3(M) / (Mean * Mean * Mean), 0.0, 1.0);
			}
			Debug.LastIsotropy = Isotropy;
			if (Settings.MaxExtraDamping > 0.0 && Settings.IsotropyThreshold > 0.0 && Isotropy < Settings.IsotropyThreshold)
			{
				const double T = 1.0 - Isotropy / Settings.IsotropyThreshold;
				Lambda += Settings.MaxExtraDamping * T * T;
			}
		}
		Debug.LastLambda = Lambda;

		FVector Y;
		if (!SolveDamped3x3(M, Lambda * Lambda, Error, Y))
		{
			break;
		}

		// Per-joint rotation vector: omega_i = w_i * (r_i x y), radians, component
		// space. All deltas come from the same linearization point, so each new
		// local rotation is computed against the *pre-step* parent rotation;
		// FK afterwards composes the deltas down the chain exactly as the
		// linear model J * dTheta = sum_i J_i * omega_i assumed.
		for (int32 i = 0; i < NumJoints - 1; ++i)
		{
			FDLSJoint& J = Joints[i];
			if (J.Weight <= 0.0)
			{
				continue;
			}
			const FVector Omega = J.Weight * FVector::CrossProduct(Effector - J.ComponentPosition, Y);
			const double Angle = Omega.Size();
			if (Angle <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}
			const double ClampedAngle = FMath::Min(Angle, Settings.MaxAngleStepRadians);
			const FQuat Delta(Omega / Angle, ClampedAngle);

			const FQuat ParentRot = (i > 0) ? Joints[i - 1].ComponentRotation : Base.GetRotation();
			J.LocalRotation = ParentRot.Inverse() * (Delta * J.ComponentRotation);
			J.LocalRotation.Normalize();

			if (J.bUseLimits)
			{
				ClampSwingTwist(J);
			}
		}

		ForwardKinematics(Joints, Base);
	}

	Debug.IterationsUsed = Iteration;
	Debug.FinalError = (TargetCS - Joints[NumJoints - 1].ComponentPosition).Size();
	if (OutDebug)
	{
		*OutDebug = Debug;
	}
	return Iteration;
}
