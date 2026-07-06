// PredictiveStopComponent.h
//
// Predictive stop trigger for path-following AI -- the fix for the "directional stop
// slides the character off the platform edge" bug.
//
// The bug: directional stops carry authored root-motion slide distance D. Vanilla path
// following runs the character all the way to the goal point and only THEN does the
// locomotion system enter the stop, so the slide plays out PAST the goal -- harmless on
// open ground, lethal when the goal sits near an edge on a platform with no rails.
//
// The fix (the core algorithm): while path following, watch the remaining distance R to
// the path end point. The stop distance D of every registered stop animation is known
// (precomputed from its root motion). Trigger the stop EARLY, when
//
//     R <= D + v * dt * LookaheadFrames
//
// so the authored slide ends exactly ON the goal instead of past it. The lookahead term
// biases the one-frame trigger quantization toward landing SHORT (safe near a ledge)
// instead of long. Closed-loop distance matching in the ABP then removes even that
// sub-frame error (see AdvancedDistanceMatchingLibrary.h and INTEGRATION.md).
//
// The sub-case (short paths): when a new move begins, the total path length L is
// classified once into a plan:
//
//     exists gait g with L >= D_dir(g) + Margin  ->  DirectionalStop (fastest such g)
//     L >= D_quick + Margin                      ->  QuickStop (cap gait to slowest)
//     otherwise                                  ->  IdleOrStep (no predictive trigger)
//
// RecommendedGaitName is broadcast so the movement/gait system can cap sprint on paths
// too short to ever stop from a sprint -- the second half of the paper algorithm.
//
// Late-trigger recovery: if the remaining distance is ALREADY inside the stop distance
// when we first see it (repath mid-run, corner deferral, move issued at close range),
// the stop still fires but starts mid-animation at the distance-matched time, so its
// remaining root motion equals the actual remaining distance.
//
// AI handshake: on trigger the move request is PAUSED (not aborted) -- root motion owns
// the capsule during the stop -- and RESUMED on completion, at which point path following
// sees the goal reached and finishes the request with a natural success, so BT MoveTo
// tasks succeed without custom plumbing.
//
// Wiring:
//   - Add to an AI-controlled ACharacter. Assign a UStopAnimSetAsset.
//   - Put a "Predictive Stop Finished" AnimNotify at the end of every stop animation
//     (or call NotifyStopFinished() from your ABP when the stop state exits).
//   - ABP: transition into the stop state on bStopActive, play ActiveStopAnim.
//     Open-loop (trigger only) already fixes the ledge bug; closed-loop via a Sequence
//     Evaluator makes the landing exact -- both wirings are in INTEGRATION.md.
//   - Root motion must actually drive the capsule during stops (root motion from
//     montages-with-slots or "Root Motion from Everything"), same as your current setup.
//
// Threading: this component ticks on the game thread (TG_PrePhysics) and only publishes
// plain properties (bStopActive, ActiveStopAnim, DistanceToStopTarget, MatchedStopTime,
// ActiveCurve). The ABP reads them via Property Access / proxy copy; nothing here is
// called from the anim worker thread.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AITypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "DistanceMatchingTypes.h"
#include "PredictiveStopComponent.generated.h"

class AAIController;
class UAnimSequence;
class UPathFollowingComponent;
class UStopAnimSetAsset;
struct FPathFollowingResult;

// A registered stop entry with its curve built and ready.
USTRUCT()
struct FRuntimeStopEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FStopAnimEntry Entry;

	UPROPERTY()
	FDistanceCurveCache Curve;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnPredictiveStopTriggered, int32, EntryIndex, FName, GaitName, FVector, StopTargetLocation, float, MatchedStartTime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPredictiveStopCompleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPredictiveStopCanceled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMovePlanEvaluated, EStopPlan, Plan, FName, RecommendedGait, float, PathLength);

// Replace YOURGAME_API with your project's generated API macro (e.g. MYGAME_API).
UCLASS(ClassGroup = (Animation), meta = (BlueprintSpawnableComponent))
class YOURGAME_API UPredictiveStopComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPredictiveStopComponent();

	// ------------------------------------------------------------------ configuration --

	// Stop animations for this character. Curves are built from it in InitializeCurves().
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Stop")
	TObjectPtr<UStopAnimSetAsset> StopAnimSet;

	// Build distance curves automatically on BeginPlay. Disable if you swap anim sets at
	// runtime and call InitializeCurves() yourself.
	UPROPERTY(EditAnywhere, Category = "Predictive Stop")
	bool bAutoBuildCurvesOnBeginPlay = true;

	// Sampling density for curve extraction. 120 Hz keeps inverse-lookup error well under
	// a centimeter at sprint speeds; there is no per-frame cost, only load-time.
	UPROPERTY(EditAnywhere, Category = "Predictive Stop", meta = (ClampMin = "10.0"))
	float CurveSampleRate = 120.f;

	// Trigger early-bias in frames of travel: fire when R <= D + Speed * Dt * this.
	// 1.0 = land short by at most one frame of travel (~10 cm at 600 cm/s @ 60 fps).
	// Raising it lands shorter/safer; closed-loop matching absorbs the slack either way.
	UPROPERTY(EditAnywhere, Category = "Predictive Stop", meta = (ClampMin = "0.0"))
	float TriggerLookaheadFrames = 1.f;

	// Below this speed (cm/s) no predictive stop fires -- normal arrival handles it.
	UPROPERTY(EditAnywhere, Category = "Predictive Stop", meta = (ClampMin = "0.0"))
	float MinTriggerSpeed = 100.f;

	// Extra path length (cm) a gait needs beyond its stop distance before the move plan
	// allows it. Budget for acceleration up to that gait plus trigger slack -- tune to
	// your movement tuning (how far the character needs to actually REACH sprint speed).
	UPROPERTY(EditAnywhere, Category = "Predictive Stop", meta = (ClampMin = "0.0"))
	float GaitCommitMargin = 150.f;

	// If the along-path and straight-line distances to the goal disagree by more than
	// this (cm), there is a corner between us and the goal. Directional stops that would
	// slide across the corner (cutting off-path -- potentially off-navmesh, i.e. off the
	// platform) are deferred or downgraded to a quick stop that fits the current segment.
	UPROPERTY(EditAnywhere, Category = "Predictive Stop", meta = (ClampMin = "0.0"))
	float CornerCutTolerance = 25.f;

	// Play-rate window for closed-loop matching (min, max). Mirrored into MatchedStopTime
	// and available to the ABP driver functions. (0,0) disables rate clamping.
	UPROPERTY(EditAnywhere, Category = "Predictive Stop")
	FVector2D PlayRateClamp = FVector2D(0.6f, 1.4f);

	// Pause the path-following request at trigger so it stops injecting acceleration while
	// root motion owns the capsule; resume on completion so the request finishes with a
	// natural success at the goal.
	UPROPERTY(EditAnywhere, Category = "Predictive Stop|AI")
	bool bPauseMoveOnTrigger = true;

	UPROPERTY(EditAnywhere, Category = "Predictive Stop|AI")
	bool bResumeMoveOnComplete = true;

	// If the move request is aborted externally (BT switched branch, new move issued)
	// while a stop is playing, cancel the stop so animation control returns immediately.
	UPROPERTY(EditAnywhere, Category = "Predictive Stop|AI")
	bool bCancelStopOnMoveAborted = true;

	// Safety net if the finished-notify never arrives: force-complete after the remaining
	// anim length * 1.5 + this slack (seconds).
	UPROPERTY(EditAnywhere, Category = "Predictive Stop", meta = (ClampMin = "0.0"))
	float CompletionTimeoutSlack = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Predictive Stop|Debug")
	bool bDrawDebug = false;

	// ---------------------------------------------------------- runtime outputs (ABP) --

	UPROPERTY(BlueprintReadOnly, Category = "Predictive Stop")
	EPredictiveStopState State = EPredictiveStopState::Inactive;

	// The ABP's stop-state transition condition.
	UPROPERTY(BlueprintReadOnly, Category = "Predictive Stop")
	bool bStopActive = false;

	// The stop animation selected at trigger time -- feed the Sequence Evaluator.
	UPROPERTY(BlueprintReadOnly, Category = "Predictive Stop")
	TObjectPtr<UAnimSequence> ActiveStopAnim;

	UPROPERTY(BlueprintReadOnly, Category = "Predictive Stop")
	EStopDirection ActiveStopDirection = EStopDirection::Forward;

	// Distance curve of the active stop -- feed the library driver functions.
	UPROPERTY(BlueprintReadOnly, Category = "Predictive Stop")
	FDistanceCurveCache ActiveCurve;

	// Live 2D distance to the stop target while Stopping (clamped to 0 once passed).
	UPROPERTY(BlueprintReadOnly, Category = "Predictive Stop")
	float DistanceToStopTarget = 0.f;

	// Component-computed distance-matched evaluator time. Simplest closed-loop wiring:
	// bind the Sequence Evaluator's Explicit Time pin to this (via the anim instance) and
	// skip the library driver functions entirely.
	UPROPERTY(BlueprintReadOnly, Category = "Predictive Stop")
	float MatchedStopTime = 0.f;

	// Path end point being stopped onto (the AI goal), captured at trigger.
	UPROPERTY(BlueprintReadOnly, Category = "Predictive Stop")
	FVector StopTargetLocation = FVector::ZeroVector;

	// Move plan for the current path (short-path sub-case output).
	UPROPERTY(BlueprintReadOnly, Category = "Predictive Stop")
	EStopPlan CurrentPlan = EStopPlan::DirectionalStop;

	// Fastest gait whose directional stop fits the current path. Consume this in your
	// gait/movement system to cap sprint on short paths.
	UPROPERTY(BlueprintReadOnly, Category = "Predictive Stop")
	FName RecommendedGaitName;

	// Live remaining along-path distance while Monitoring (debug/UI).
	UPROPERTY(BlueprintReadOnly, Category = "Predictive Stop")
	float RemainingPathDistance = 0.f;

	// -------------------------------------------------------------------------- events --

	UPROPERTY(BlueprintAssignable, Category = "Predictive Stop")
	FOnPredictiveStopTriggered OnStopTriggered;

	UPROPERTY(BlueprintAssignable, Category = "Predictive Stop")
	FOnPredictiveStopCompleted OnStopCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Predictive Stop")
	FOnPredictiveStopCanceled OnStopCanceled;

	// Fired whenever a new path is classified. Hook your gait cap here.
	UPROPERTY(BlueprintAssignable, Category = "Predictive Stop")
	FOnMovePlanEvaluated OnMovePlanEvaluated;

	// ---------------------------------------------------------------------------- api --

	// (Re)build distance curves for every entry in StopAnimSet. Game thread only.
	UFUNCTION(BlueprintCallable, Category = "Predictive Stop")
	void InitializeCurves();

	// Completion signal -- call from the "Predictive Stop Finished" AnimNotify at the end
	// of each stop anim, or from the ABP when the stop state exits.
	UFUNCTION(BlueprintCallable, Category = "Predictive Stop")
	void NotifyStopFinished();

	// Abandon the stop without resuming the move (external system took over).
	UFUNCTION(BlueprintCallable, Category = "Predictive Stop")
	void CancelStop();

	// Authored stop distance for a gait (max over its directional entries), or 0 if none.
	UFUNCTION(BlueprintPure, Category = "Predictive Stop")
	float GetStopDistanceForGait(FName GaitName) const;

	// Fastest gait whose directional stop + margin fits PathLength, or None.
	UFUNCTION(BlueprintPure, Category = "Predictive Stop")
	FName GetMaxGaitForPathLength(float PathLength) const;

	// Matched time into the ACTIVE stop for an arbitrary distance (e.g. montage start).
	UFUNCTION(BlueprintPure, Category = "Predictive Stop")
	float GetMatchedTimeForDistance(float Distance) const;

	// Braking-based stop distance for the owning character's CURRENT velocity and
	// CharacterMovement tuning -- the player-side / no-path prediction path.
	UFUNCTION(BlueprintPure, Category = "Predictive Stop")
	float PredictBrakingStopDistance() const;

	// -------------------------------------------------------------------- component --

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// Per-gait planning row derived from the directional entries.
	struct FGaitPlanInfo
	{
		FName GaitName;
		float ReferenceSpeed = 0.f;
		float StopDistance = 0.f; // max over the gait's directional entries (conservative)
	};

	void UpdateMonitoring(float DeltaTime);
	void UpdateStopping(float DeltaTime);

	// Classify a fresh path into CurrentPlan / RecommendedGaitName and broadcast.
	void EvaluateMovePlan(float TotalPathLength);

	// Remaining distances from the live path. Returns false if there is no usable path.
	bool ComputeRemainingPathDistance(const UPathFollowingComponent& PathFollowing, float& OutAlongPath, float& OutStraightLine, float& OutDistToNextPoint, bool& bOutFinalSegment) const;

	// Velocity direction relative to facing, quantized to 4 quadrants (+/-45, +/-135).
	EStopDirection ComputeStopDirection(const FVector& Velocity) const;

	// Candidate selection with fit, corner, and late-recovery rules. INDEX_NONE = defer.
	int32 SelectStopEntry(EStopDirection Direction, float Speed, float RemainingAlongPath, float DistToNextPoint, bool bOnFinalSegment, float StraightLineToGoal, float Lookahead) const;

	void TriggerStop(int32 EntryIndex, float RemainingDistance, UPathFollowingComponent& PathFollowing);
	void CompleteStop();
	void ResetStopOutputs();

	void BuildGaitTable(TArray<FGaitPlanInfo>& OutGaits) const;
	float GetMinQuickStopDistance() const;

	UPathFollowingComponent* ResolvePathFollowing();
	void HandleMoveRequestFinished(FAIRequestID RequestID, const FPathFollowingResult& Result);

	void DrawDebugInfo() const;

	UPROPERTY(Transient)
	TArray<FRuntimeStopEntry> RuntimeEntries;

	// New-move detection: a path is "new" when its object or its goal changes.
	const void* CachedPathPtr = nullptr;
	FVector CachedGoal = FVector::ZeroVector;

	TWeakObjectPtr<UPathFollowingComponent> BoundPathFollowing;
	FDelegateHandle RequestFinishedHandle;
	FAIRequestID PausedRequestId;

	int32 ActiveEntryIndex = INDEX_NONE;
	float StopElapsed = 0.f;
	float StopTimeout = 0.f;

	// Debug-draw snapshot of the last monitoring evaluation.
	int32 LastCandidateIndex = INDEX_NONE;
	float LastCandidateStopDistance = 0.f;
};

// Drop this notify at the very end of every stop animation. It routes completion back to
// the component so the move request can resume and bStopActive clears.
UCLASS(meta = (DisplayName = "Predictive Stop Finished"))
class YOURGAME_API UAnimNotify_PredictiveStopFinished : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
