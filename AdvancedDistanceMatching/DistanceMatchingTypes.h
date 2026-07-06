// DistanceMatchingTypes.h
//
// Shared types for the AdvancedDistanceMatching system -- a from-scratch, plugin-free
// replacement for UE's AnimationLocomotionLibrary distance matching, extended to be
// path-aware and root-motion-aware.
//
// The core data structure here is FDistanceCurveCache: a precomputed "distance remaining
// to the stop point" curve for one stop animation. It answers the two questions the whole
// system is built on:
//
//   1. "How far will this stop animation carry the character?"        -> TotalDistance
//   2. "At what anim time does exactly D cm of travel remain?"        -> GetTimeForRemainingDistance(D)
//
// Question 1 drives the PREDICTIVE TRIGGER (fire the stop early so the authored slide
// ends exactly on the AI path point instead of past it -- the ledge bug fix).
// Question 2 drives CLOSED-LOOP DISTANCE MATCHING (scrub a Sequence Evaluator so the
// pose always agrees with the actual remaining distance) and LATE-TRIGGER RECOVERY
// (enter the stop anim mid-way when the trigger window was missed, e.g. after a repath).
//
// Curves are built once on the game thread (see UAdvancedDistanceMatchingLibrary), either
// by sampling the sequence's root motion or by reading an authored "Distance" float curve.
// Lookups are pure, allocation-free, and safe to call from the anim worker thread.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DistanceMatchingTypes.generated.h"

class UAnimSequence;

// Which way the character is moving relative to its facing when the stop fires.
// Mapped from local-space velocity with fixed +/-45 / +/-135 degree quadrants.
UENUM(BlueprintType)
enum class EStopDirection : uint8
{
	Forward,
	Backward,
	Left,
	Right
};

UENUM(BlueprintType)
enum class EPredictiveStopState : uint8
{
	// No active path following (or component unconfigured).
	Inactive,
	// Path following is running; watching remaining distance for the trigger point.
	Monitoring,
	// Stop has been triggered; stop anim owns the character until NotifyStopFinished().
	Stopping
};

// Decided once per move request, when a new path is detected. This is the formalization
// of the "path shorter than the authored stop" sub-case.
UENUM(BlueprintType)
enum class EStopPlan : uint8
{
	// Path is long enough to reach the recommended gait AND play its full directional stop.
	DirectionalStop,
	// Too short for any directional stop, but long enough for a quick stop.
	QuickStop,
	// Shorter than even a quick stop. No predictive trigger; let normal path-following
	// arrival handle it (idle / small step). Engine braking does not overshoot, so this
	// tier is inherently ledge-safe.
	IdleOrStep
};

// Precomputed distance-matching curve for one stop animation.
//
// RemainingDistances[i] = horizontal distance (cm) between the root position at Times[i]
// and the root position at the END of the animation. Monotonically non-increasing by
// construction (enforced at build time), which makes the inverse lookup a simple binary
// search. Times are uniform across [0, PlayLength].
USTRUCT(BlueprintType)
struct YOURGAME_API FDistanceCurveCache
{
	GENERATED_BODY()

	// Uniform sample times, [0 .. PlayLength].
	UPROPERTY(BlueprintReadOnly, Category = "Distance Matching")
	TArray<float> Times;

	// Distance still to be traveled at each sample time. Non-increasing, ends at 0.
	UPROPERTY(BlueprintReadOnly, Category = "Distance Matching")
	TArray<float> RemainingDistances;

	// Total horizontal distance the animation's root motion covers (== RemainingDistances[0]).
	// This IS the "authored directional stop distance" used by the predictive trigger.
	UPROPERTY(BlueprintReadOnly, Category = "Distance Matching")
	float TotalDistance = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Distance Matching")
	float PlayLength = 0.f;

	bool IsValid() const
	{
		return Times.Num() >= 2 && Times.Num() == RemainingDistances.Num() && PlayLength > 0.f;
	}

	// Inverse lookup: earliest anim time at which exactly RemainingDistance is left to travel.
	//   RemainingDistance >= TotalDistance -> 0 (start of anim)
	//   RemainingDistance <= 0             -> first time the root stops translating
	// "Earliest" matters: stops often end with a translation-free settle tail; matching to
	// the first zero lets the tail play out at natural speed via the play-rate clamp.
	float GetTimeForRemainingDistance(float RemainingDistance) const;

	// Forward lookup: distance still to be traveled at anim time Time.
	float GetRemainingDistanceAtTime(float Time) const;
};

// One stop animation registered with the system.
USTRUCT(BlueprintType)
struct FStopAnimEntry
{
	GENERATED_BODY()

	// The stop animation. Must have root motion enabled -- the authored slide is what we
	// are measuring and matching against.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stop")
	TObjectPtr<UAnimSequence> StopAnim;

	// Project-defined gait label ("Walk", "Jog", "Sprint", ...). Used to group entries for
	// move planning (gait capping on short paths) -- the component never interprets the
	// name itself, it only matches and sorts by ReferenceSpeed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stop")
	FName GaitName = TEXT("Jog");

	// Ground speed (cm/s) this stop was authored for. Entry selection picks the entry whose
	// ReferenceSpeed is closest to the actual speed at trigger time.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stop", meta = (ClampMin = "0.0"))
	float ReferenceSpeed = 350.f;

	// Movement direction this stop was authored for (velocity relative to facing).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stop")
	EStopDirection Direction = EStopDirection::Forward;

	// Quick stops are the short-distance fallback tier used when the path (or the remaining
	// distance after a repath) is too short for a directional stop.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stop")
	bool bQuickStop = false;

	// If true, read the distance curve from a float curve authored inside the animation
	// (DistanceCurveName) instead of sampling root motion. Supports both sign conventions:
	// UE-style negative-counting-up-to-zero and positive-remaining-decreasing-to-zero.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stop")
	bool bUseAuthoredDistanceCurve = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stop", meta = (EditCondition = "bUseAuthoredDistanceCurve"))
	FName DistanceCurveName = TEXT("Distance");
};

// Shareable collection of stop entries for one character archetype.
// Replace YOURGAME_API with your project's generated API macro (e.g. MYGAME_API).
UCLASS(BlueprintType)
class YOURGAME_API UStopAnimSetAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// Typical set: 4 directions x 2-3 gaits of directional stops + 1-4 quick stops.
	// A single Forward entry per gait is a perfectly fine starting point for AI, since
	// path-following AI almost always rotates to face its movement.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stops")
	TArray<FStopAnimEntry> Entries;
};
