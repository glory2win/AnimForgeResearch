// AdvancedDistanceMatchingLibrary.h
//
// From-scratch distance matching function library -- what the engine's
// "AnimationLocomotionLibrary" plugin provides (UAnimDistanceMatchingLibrary /
// UAnimCharacterMovementLibrary), re-implemented so the project owns the tech, plus
// extensions the plugin does not have:
//
//   - AUTOMATIC distance-curve extraction from root motion. The plugin requires a
//     "Distance" float curve authored into every animation; here the curve is built at
//     load time by sampling the sequence's root motion track, so existing directional
//     stops work with zero re-authoring. Authored curves are still supported (both the
//     UE negative-to-zero and positive-to-zero sign conventions).
//
//   - Root-motion-aware matching. Classic distance matching drives a capsule with the
//     movement component and scrubs a non-root-motion anim to match. Our stops DRIVE the
//     capsule via root motion, which inverts the loop: anim time -> root motion delta ->
//     capsule moves -> distance shrinks -> anim time. That feedback loop is stable as
//     long as time advancement is play-rate-clamped and monotonic, which is exactly what
//     AdvanceTimeClamped / DistanceMatchStopToTarget enforce.
//
// Three groups of functions:
//
//   1. Curve building (GAME THREAD ONLY -- samples animation data, call at BeginPlay or
//      on asset registration, never per frame and never from the anim worker thread).
//
//   2. Pure lookups (thread-safe, allocation-free -- callable from anywhere, including
//      BlueprintThreadSafeUpdateAnimation and anim node functions).
//
//   3. Sequence Evaluator drivers (thread-safe, for use in anim node functions "On
//      Initial Update" / "On Update" of a Sequence Evaluator node). These mirror the
//      plugin's DistanceMatchToTarget/AdvanceTimeByDistanceMatching API shape and only
//      need AnimGraphRuntime, which is a stock engine module, not a plugin.
//
// IMPORTANT Sequence Evaluator gotchas (also in INTEGRATION.md):
//   - "Teleport to Explicit Time" must be FALSE on the evaluator node, otherwise time
//     changes are treated as teleports: no root motion is extracted and notifies skip.
//   - The Explicit Time pin must NOT be exposed/connected if you use the driver functions
//     below -- a connected pin overwrites SetExplicitTime every update (the engine's own
//     plugin has the same constraint).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimExecutionContext.h"
#include "SequenceEvaluatorLibrary.h"
#include "DistanceMatchingTypes.h"
#include "AdvancedDistanceMatchingLibrary.generated.h"

class UAnimSequence;

// Replace YOURGAME_API with your project's generated API macro (e.g. MYGAME_API).
UCLASS()
class YOURGAME_API UAdvancedDistanceMatchingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------------------------
	// 1. Curve building -- game thread only.
	// ---------------------------------------------------------------------------------

	// Builds the distance curve by sampling the sequence's root motion at SampleRate Hz.
	// RemainingDistances[i] = 2D distance from the root position at sample i to the final
	// root position, then forced monotonic (running max, back to front) so small authored
	// overshoot/settle wiggles can't break the inverse lookup.
	// Returns false if the sequence is null, has no length, or its root barely moves.
	UFUNCTION(BlueprintCallable, Category = "Distance Matching|Setup")
	static bool BuildDistanceCurveFromRootMotion(const UAnimSequence* Sequence, FDistanceCurveCache& OutCurve, float SampleRate = 120.f);

	// Builds the distance curve from a float curve authored inside the animation.
	// Handles both conventions: UE-style stops (negative values counting up to 0 at the
	// stop point) are negated on import; positive-decreasing curves are used as-is.
	UFUNCTION(BlueprintCallable, Category = "Distance Matching|Setup")
	static bool BuildDistanceCurveFromAuthoredCurve(const UAnimSequence* Sequence, FName CurveName, FDistanceCurveCache& OutCurve, float SampleRate = 120.f);

	// ---------------------------------------------------------------------------------
	// 2. Pure lookups -- thread-safe.
	// ---------------------------------------------------------------------------------

	// Earliest anim time at which exactly RemainingDistance is left to travel.
	UFUNCTION(BlueprintPure, Category = "Distance Matching", meta = (BlueprintThreadSafe))
	static float GetTimeForRemainingDistance(const FDistanceCurveCache& Curve, float RemainingDistance);

	// Distance still to be traveled at anim time Time.
	UFUNCTION(BlueprintPure, Category = "Distance Matching", meta = (BlueprintThreadSafe))
	static float GetRemainingDistanceAtTime(const FDistanceCurveCache& Curve, float Time);

	// Total root-motion travel of the animation == the authored stop distance the
	// predictive trigger subtracts from the remaining path length.
	UFUNCTION(BlueprintPure, Category = "Distance Matching", meta = (BlueprintThreadSafe))
	static float GetTotalStopDistance(const FDistanceCurveCache& Curve);

	// The closed-loop step. Returns the new evaluator time for this frame:
	//     Target = Curve^-1(DistanceRemaining)
	//     NewT   = clamp(Target, T + MinRate*Dt, T + MaxRate*Dt), never below T, never past PlayLength
	// The play-rate clamp keeps the anim close to authored timing (visual quality), makes
	// the root-motion feedback loop stable, and guarantees forward progress even if the
	// character gets physically blocked (distance stops shrinking but time still advances
	// at MinRate, so the stop always finishes). Pass PlayRateClamp = (0,0) to disable
	// clamping and only enforce monotonic time.
	UFUNCTION(BlueprintPure, Category = "Distance Matching", meta = (BlueprintThreadSafe))
	static float AdvanceTimeClamped(const FDistanceCurveCache& Curve, float CurrentTime, float DistanceRemaining, float DeltaTime, FVector2D PlayRateClamp);

	// Closed-form ground braking distance -- our equivalent of the plugin's
	// PredictGroundMovementStopLocation, for player characters that stop via
	// CharacterMovement braking instead of a nav path. Derivation in DESIGN.md:
	//     v' = -f*v - d   =>   StopDistance = v/f - (d/f^2) * ln(1 + f*v/d)
	// with the analytic limits f->0 (v^2/2d) and d->0 (v/f) handled explicitly.
	// Feed it the values from your UCharacterMovementComponent.
	UFUNCTION(BlueprintPure, Category = "Distance Matching", meta = (BlueprintThreadSafe))
	static float PredictGroundMovementStopDistance(FVector Velocity, bool bUseSeparateBrakingFriction, float GroundFriction, float BrakingFriction, float BrakingFrictionFactor, float BrakingDecelerationWalking);

	// ---------------------------------------------------------------------------------
	// 3. Sequence Evaluator drivers -- thread-safe, anim node functions only.
	// ---------------------------------------------------------------------------------

	// Call from the Sequence Evaluator's "On Initial Update". Sets the sequence and the
	// distance-matched START time: if the trigger fired late (repath, corner defer) and
	// DistanceRemaining is already smaller than the anim's total travel, the stop begins
	// mid-animation so its remaining root motion still equals the actual remaining
	// distance -- the late-trigger recovery path.
	UFUNCTION(BlueprintCallable, Category = "Distance Matching", meta = (BlueprintThreadSafe))
	static void InitializeStopEvaluator(const FSequenceEvaluatorReference& SequenceEvaluator, UAnimSequence* Sequence, const FDistanceCurveCache& Curve, float DistanceRemaining);

	// Call from the Sequence Evaluator's "On Update" every frame while the stop state is
	// active. Applies AdvanceTimeClamped to the evaluator's explicit time using the live
	// DistanceRemaining (feed it PredictiveStopComponent.DistanceToStopTarget).
	UFUNCTION(BlueprintCallable, Category = "Distance Matching", meta = (BlueprintThreadSafe))
	static void DistanceMatchStopToTarget(const FAnimUpdateContext& UpdateContext, const FSequenceEvaluatorReference& SequenceEvaluator, const FDistanceCurveCache& Curve, float DistanceRemaining, FVector2D PlayRateClamp);
};
