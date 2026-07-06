// AdvancedDistanceMatchingLibrary.cpp

#include "AdvancedDistanceMatchingLibrary.h"

#include "Animation/AnimSequence.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAdvancedDistanceMatching, Log, All);

namespace
{
	// Single call site for the engine's root motion extraction so engine-version churn is
	// a one-line fix. UAnimSequence::ExtractRootMotionFromRange(float, float) is the
	// long-standing API (UE4 through UE 5.5). If your engine version has deprecated it in
	// favor of the FAnimExtractContext overload, swap the call here.
	FTransform ExtractRootMotionRange(const UAnimSequence& Sequence, float StartTime, float EndTime)
	{
		return Sequence.ExtractRootMotionFromRange(StartTime, EndTime);
	}

	// Shared tail of both build paths: enforce monotonic non-increasing values and fill
	// the summary fields. Monotonicity is required for the binary-search inverse lookup;
	// authored stops routinely have a few cm of overshoot-and-settle wobble at the end,
	// and the running max (back to front) flattens that into a safe plateau.
	bool FinalizeCurve(FDistanceCurveCache& Curve, const UAnimSequence& Sequence)
	{
		for (int32 i = Curve.RemainingDistances.Num() - 2; i >= 0; --i)
		{
			Curve.RemainingDistances[i] = FMath::Max(Curve.RemainingDistances[i], Curve.RemainingDistances[i + 1]);
		}

		Curve.TotalDistance = Curve.RemainingDistances.Num() > 0 ? Curve.RemainingDistances[0] : 0.f;
		Curve.PlayLength = Sequence.GetPlayLength();

		if (Curve.TotalDistance <= KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogAdvancedDistanceMatching, Warning,
				TEXT("Distance curve for '%s' covers ~0 cm. A stop with no root translation cannot be distance matched."),
				*Sequence.GetName());
			Curve = FDistanceCurveCache();
			return false;
		}
		return true;
	}
}

bool UAdvancedDistanceMatchingLibrary::BuildDistanceCurveFromRootMotion(const UAnimSequence* Sequence, FDistanceCurveCache& OutCurve, float SampleRate)
{
	OutCurve = FDistanceCurveCache();

	if (!Sequence || Sequence->GetPlayLength() <= 0.f)
	{
		UE_LOG(LogAdvancedDistanceMatching, Warning, TEXT("BuildDistanceCurveFromRootMotion: invalid sequence."));
		return false;
	}

	// Extraction reads the root track regardless, but at runtime the stop will only move
	// the capsule if root motion is actually enabled on the asset -- flag it now instead
	// of debugging a character that stops dead at the trigger point later.
	if (!Sequence->HasRootMotion())
	{
		UE_LOG(LogAdvancedDistanceMatching, Warning,
			TEXT("'%s' does not have root motion enabled. The distance curve will be built from the root track, ")
			TEXT("but the stop will not drive the capsule until EnableRootMotion is set on the asset."),
			*Sequence->GetName());
	}

	const float Length = Sequence->GetPlayLength();
	const int32 NumSamples = FMath::Max(2, FMath::CeilToInt32(Length * FMath::Max(SampleRate, 10.f)) + 1);
	const float Step = Length / static_cast<float>(NumSamples - 1);

	// Accumulate the root's path by composing per-interval deltas. Each delta is expressed
	// in the root's frame at the interval start, so translation must be rotated by the
	// accumulated rotation before summing -- done explicitly (position + quat) rather than
	// with FTransform multiplication to keep the composition order unambiguous.
	TArray<FVector> Positions;
	Positions.Reserve(NumSamples);

	FVector Position = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	Positions.Add(Position);

	for (int32 i = 1; i < NumSamples; ++i)
	{
		const float T0 = static_cast<float>(i - 1) * Step;
		const float T1 = FMath::Min(static_cast<float>(i) * Step, Length);

		const FTransform Delta = ExtractRootMotionRange(*Sequence, T0, T1);
		Position += Rotation.RotateVector(Delta.GetTranslation());
		Rotation = (Rotation * Delta.GetRotation()).GetNormalized();
		Positions.Add(Position);
	}

	// Remaining distance = straight-line 2D distance from the pose at time t to the FINAL
	// root position. Straight-line (not arc length) is the correct measure because the
	// gameplay quantity we match against is "distance to the stop target point".
	const FVector FinalPosition = Positions.Last();

	OutCurve.Times.SetNum(NumSamples);
	OutCurve.RemainingDistances.SetNum(NumSamples);
	for (int32 i = 0; i < NumSamples; ++i)
	{
		OutCurve.Times[i] = static_cast<float>(i) * Step;
		OutCurve.RemainingDistances[i] = FVector::Dist2D(Positions[i], FinalPosition);
	}

	return FinalizeCurve(OutCurve, *Sequence);
}

bool UAdvancedDistanceMatchingLibrary::BuildDistanceCurveFromAuthoredCurve(const UAnimSequence* Sequence, FName CurveName, FDistanceCurveCache& OutCurve, float SampleRate)
{
	OutCurve = FDistanceCurveCache();

	if (!Sequence || Sequence->GetPlayLength() <= 0.f || CurveName.IsNone())
	{
		UE_LOG(LogAdvancedDistanceMatching, Warning, TEXT("BuildDistanceCurveFromAuthoredCurve: invalid sequence or curve name."));
		return false;
	}

	// NOTE(engine version): HasCurveData/EvaluateCurveData(FName, float) are the stable
	// UAnimSequenceBase entry points through UE 5.5; adjust here if your version moved to
	// the FAnimExtractContext overloads.
	if (!Sequence->HasCurveData(CurveName, false))
	{
		UE_LOG(LogAdvancedDistanceMatching, Warning, TEXT("'%s' has no float curve named '%s'."),
			*Sequence->GetName(), *CurveName.ToString());
		return false;
	}

	const float Length = Sequence->GetPlayLength();
	const int32 NumSamples = FMath::Max(2, FMath::CeilToInt32(Length * FMath::Max(SampleRate, 10.f)) + 1);
	const float Step = Length / static_cast<float>(NumSamples - 1);

	OutCurve.Times.SetNum(NumSamples);
	OutCurve.RemainingDistances.SetNum(NumSamples);
	for (int32 i = 0; i < NumSamples; ++i)
	{
		const float Time = FMath::Min(static_cast<float>(i) * Step, Length);
		OutCurve.Times[i] = Time;
		OutCurve.RemainingDistances[i] = Sequence->EvaluateCurveData(CurveName, Time);
	}

	// Sign convention: UE-style stop curves are authored negative, counting up to 0 at the
	// stop point. Normalize to positive-remaining so both conventions behave identically.
	if (OutCurve.RemainingDistances[0] < 0.f)
	{
		for (float& Value : OutCurve.RemainingDistances)
		{
			Value = -Value;
		}
	}

	return FinalizeCurve(OutCurve, *Sequence);
}

float UAdvancedDistanceMatchingLibrary::GetTimeForRemainingDistance(const FDistanceCurveCache& Curve, float RemainingDistance)
{
	return Curve.GetTimeForRemainingDistance(RemainingDistance);
}

float UAdvancedDistanceMatchingLibrary::GetRemainingDistanceAtTime(const FDistanceCurveCache& Curve, float Time)
{
	return Curve.GetRemainingDistanceAtTime(Time);
}

float UAdvancedDistanceMatchingLibrary::GetTotalStopDistance(const FDistanceCurveCache& Curve)
{
	return Curve.TotalDistance;
}

float UAdvancedDistanceMatchingLibrary::AdvanceTimeClamped(const FDistanceCurveCache& Curve, float CurrentTime, float DistanceRemaining, float DeltaTime, FVector2D PlayRateClamp)
{
	if (!Curve.IsValid())
	{
		return CurrentTime;
	}

	float Target = Curve.GetTimeForRemainingDistance(DistanceRemaining);

	const bool bClampEnabled = DeltaTime > 0.f && PlayRateClamp.Y > 0.f;
	if (bClampEnabled)
	{
		Target = FMath::Clamp(Target,
			CurrentTime + DeltaTime * FMath::Max(PlayRateClamp.X, 0.f),
			CurrentTime + DeltaTime * PlayRateClamp.Y);
	}

	// Never rewind (a stop scrubbing backwards reads as a glitch, and root motion would
	// extract a backwards delta), never run past the end.
	return FMath::Clamp(FMath::Max(Target, CurrentTime), 0.f, Curve.PlayLength);
}

float UAdvancedDistanceMatchingLibrary::PredictGroundMovementStopDistance(FVector Velocity, bool bUseSeparateBrakingFriction, float GroundFriction, float BrakingFriction, float BrakingFrictionFactor, float BrakingDecelerationWalking)
{
	const float Speed = Velocity.Size2D();
	if (Speed <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}

	// Mirrors UCharacterMovementComponent::ApplyVelocityBraking's inputs:
	//   v' = -f*v - d,  f = (effective friction) * factor,  d = braking deceleration
	const float Friction = FMath::Max((bUseSeparateBrakingFriction ? BrakingFriction : GroundFriction) * BrakingFrictionFactor, 0.f);
	const float Decel = FMath::Max(BrakingDecelerationWalking, 0.f);

	if (Friction <= KINDA_SMALL_NUMBER && Decel <= KINDA_SMALL_NUMBER)
	{
		return TNumericLimits<float>::Max(); // Nothing brakes; the character never stops.
	}
	if (Friction <= KINDA_SMALL_NUMBER)
	{
		return FMath::Square(Speed) / (2.f * Decel); // Pure constant deceleration.
	}
	if (Decel <= KINDA_SMALL_NUMBER)
	{
		return Speed / Friction; // Pure exponential friction decay: integral of v0*e^(-f*t).
	}

	// Full closed form (derivation in DESIGN.md):
	//   StopDistance = v/f - (d/f^2) * ln(1 + f*v/d)
	return Speed / Friction - (Decel / FMath::Square(Friction)) * FMath::Loge(1.f + Friction * Speed / Decel);
}

void UAdvancedDistanceMatchingLibrary::InitializeStopEvaluator(const FSequenceEvaluatorReference& SequenceEvaluator, UAnimSequence* Sequence, const FDistanceCurveCache& Curve, float DistanceRemaining)
{
	SequenceEvaluator.CallAnimNodeFunction<FAnimNode_SequenceEvaluatorBase>(
		TEXT("InitializeStopEvaluator"),
		[&](FAnimNode_SequenceEvaluatorBase& Node)
		{
			if (Sequence && !Node.SetSequence(Sequence))
			{
				UE_LOG(LogAdvancedDistanceMatching, Warning,
					TEXT("InitializeStopEvaluator: could not set sequence -- unexpose the evaluator's Sequence pin to set it dynamically."));
			}

			const float StartTime = Curve.IsValid() ? Curve.GetTimeForRemainingDistance(DistanceRemaining) : 0.f;
			if (!Node.SetExplicitTime(StartTime))
			{
				UE_LOG(LogAdvancedDistanceMatching, Warning,
					TEXT("InitializeStopEvaluator: could not set explicit time -- the Explicit Time pin must not be connected."));
			}
		});
}

void UAdvancedDistanceMatchingLibrary::DistanceMatchStopToTarget(const FAnimUpdateContext& UpdateContext, const FSequenceEvaluatorReference& SequenceEvaluator, const FDistanceCurveCache& Curve, float DistanceRemaining, FVector2D PlayRateClamp)
{
	SequenceEvaluator.CallAnimNodeFunction<FAnimNode_SequenceEvaluatorBase>(
		TEXT("DistanceMatchStopToTarget"),
		[&](FAnimNode_SequenceEvaluatorBase& Node)
		{
			if (!Curve.IsValid())
			{
				return;
			}

			const FAnimationUpdateContext* Context = UpdateContext.GetContext();
			const float DeltaTime = Context ? Context->GetDeltaTime() : 0.f;

			const float NewTime = AdvanceTimeClamped(Curve, Node.GetExplicitTime(), DistanceRemaining, DeltaTime, PlayRateClamp);
			if (!Node.SetExplicitTime(NewTime))
			{
				UE_LOG(LogAdvancedDistanceMatching, Warning,
					TEXT("DistanceMatchStopToTarget: could not set explicit time -- the Explicit Time pin must not be connected."));
			}
		});
}
