// PredictiveStopComponent.cpp

#include "PredictiveStopComponent.h"

#include "AdvancedDistanceMatchingLibrary.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogPredictiveStop, Log, All);

UPredictiveStopComponent::UPredictiveStopComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Publish outputs before animation consumes them this frame.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UPredictiveStopComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoBuildCurvesOnBeginPlay)
	{
		InitializeCurves();
	}
}

void UPredictiveStopComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UPathFollowingComponent* PathFollowing = BoundPathFollowing.Get())
	{
		PathFollowing->OnRequestFinished.Remove(RequestFinishedHandle);
	}
	BoundPathFollowing.Reset();

	Super::EndPlay(EndPlayReason);
}

void UPredictiveStopComponent::InitializeCurves()
{
	RuntimeEntries.Reset();
	ResetStopOutputs();
	State = EPredictiveStopState::Inactive;

	if (!StopAnimSet)
	{
		UE_LOG(LogPredictiveStop, Warning, TEXT("%s: no StopAnimSet assigned."), *GetNameSafe(GetOwner()));
		return;
	}

	for (int32 i = 0; i < StopAnimSet->Entries.Num(); ++i)
	{
		const FStopAnimEntry& Entry = StopAnimSet->Entries[i];

		FRuntimeStopEntry Runtime;
		Runtime.Entry = Entry;

		const bool bBuilt = Entry.bUseAuthoredDistanceCurve
			? UAdvancedDistanceMatchingLibrary::BuildDistanceCurveFromAuthoredCurve(Entry.StopAnim, Entry.DistanceCurveName, Runtime.Curve, CurveSampleRate)
			: UAdvancedDistanceMatchingLibrary::BuildDistanceCurveFromRootMotion(Entry.StopAnim, Runtime.Curve, CurveSampleRate);

		if (bBuilt)
		{
			RuntimeEntries.Add(MoveTemp(Runtime));
			UE_LOG(LogPredictiveStop, Log, TEXT("%s: registered stop '%s' (gait %s, %s%s) -- distance %.1f cm over %.2f s."),
				*GetNameSafe(GetOwner()), *GetNameSafe(Entry.StopAnim), *Entry.GaitName.ToString(),
				*UEnum::GetValueAsString(Entry.Direction), Entry.bQuickStop ? TEXT(", quick") : TEXT(""),
				RuntimeEntries.Last().Curve.TotalDistance, RuntimeEntries.Last().Curve.PlayLength);
		}
		else
		{
			UE_LOG(LogPredictiveStop, Warning, TEXT("%s: skipped stop entry %d ('%s') -- distance curve build failed."),
				*GetNameSafe(GetOwner()), i, *GetNameSafe(Entry.StopAnim));
		}
	}
}

void UPredictiveStopComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (State == EPredictiveStopState::Stopping)
	{
		UpdateStopping(DeltaTime);
	}
	else
	{
		UpdateMonitoring(DeltaTime);
	}

#if ENABLE_DRAW_DEBUG
	if (bDrawDebug)
	{
		DrawDebugInfo();
	}
#endif
}

// ------------------------------------------------------------------------- monitoring --

void UPredictiveStopComponent::UpdateMonitoring(float DeltaTime)
{
	UPathFollowingComponent* PathFollowing = ResolvePathFollowing();
	if (!PathFollowing || RuntimeEntries.Num() == 0)
	{
		State = EPredictiveStopState::Inactive;
		return;
	}

	if (PathFollowing->GetStatus() != EPathFollowingStatus::Moving)
	{
		// Between moves. Forget the cached path so the next request re-plans.
		State = EPredictiveStopState::Inactive;
		CachedPathPtr = nullptr;
		RemainingPathDistance = 0.f;
		return;
	}

	float AlongPath = 0.f, StraightLine = 0.f, DistToNextPoint = 0.f;
	bool bFinalSegment = false;
	if (!ComputeRemainingPathDistance(*PathFollowing, AlongPath, StraightLine, DistToNextPoint, bFinalSegment))
	{
		return;
	}

	// New move / repath detection: the plan (gait cap, stop tier) is per-path, so any
	// change of path object or goal re-classifies. A moving goal re-plans continuously,
	// which is intended -- the remaining length keeps changing.
	const FNavPathSharedPtr Path = PathFollowing->GetPath();
	const FVector Goal = Path->GetPathPoints().Last().Location;
	if (State == EPredictiveStopState::Inactive || CachedPathPtr != Path.Get() || !Goal.Equals(CachedGoal, 10.f))
	{
		CachedPathPtr = Path.Get();
		CachedGoal = Goal;
		EvaluateMovePlan(AlongPath);
	}

	State = EPredictiveStopState::Monitoring;
	RemainingPathDistance = AlongPath;

	if (CurrentPlan == EStopPlan::IdleOrStep)
	{
		// Path shorter than even a quick stop: no predictive trigger. Engine braking
		// arrival cannot overshoot, so this tier is already ledge-safe.
		return;
	}

	const FVector Velocity = GetOwner()->GetVelocity();
	const float Speed = Velocity.Size2D();
	if (Speed < MinTriggerSpeed)
	{
		return;
	}

	const float Lookahead = Speed * DeltaTime * TriggerLookaheadFrames;
	const EStopDirection Direction = ComputeStopDirection(Velocity);

	const int32 CandidateIndex = SelectStopEntry(Direction, Speed, AlongPath, DistToNextPoint, bFinalSegment, StraightLine, Lookahead);
	LastCandidateIndex = CandidateIndex;
	LastCandidateStopDistance = CandidateIndex != INDEX_NONE ? RuntimeEntries[CandidateIndex].Curve.TotalDistance : 0.f;

	if (CandidateIndex == INDEX_NONE)
	{
		return;
	}

	// THE trigger rule. The lookahead term fires one frame early, so quantization error
	// lands the character SHORT of the goal (never past it -- never off the ledge);
	// closed-loop matching then closes that final gap.
	if (AlongPath <= RuntimeEntries[CandidateIndex].Curve.TotalDistance + Lookahead)
	{
		TriggerStop(CandidateIndex, AlongPath, *PathFollowing);
	}
}

bool UPredictiveStopComponent::ComputeRemainingPathDistance(const UPathFollowingComponent& PathFollowing, float& OutAlongPath, float& OutStraightLine, float& OutDistToNextPoint, bool& bOutFinalSegment) const
{
	const FNavPathSharedPtr Path = PathFollowing.GetPath();
	if (!Path.IsValid())
	{
		return false;
	}

	const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
	if (Points.Num() < 2)
	{
		return false;
	}

	const FVector Position = GetOwner()->GetActorLocation();
	const int32 NextIndex = FMath::Clamp(static_cast<int32>(PathFollowing.GetNextPathIndex()), 0, Points.Num() - 1);

	// Remaining = my distance to the next path point + the untraveled segments after it.
	// All 2D: vertical offsets (capsule half-height vs nav point Z) must not inflate the
	// horizontal distance the stop's root motion is measured in.
	OutDistToNextPoint = FVector::Dist2D(Position, Points[NextIndex].Location);

	float Along = OutDistToNextPoint;
	for (int32 i = NextIndex; i < Points.Num() - 1; ++i)
	{
		Along += FVector::Dist2D(Points[i].Location, Points[i + 1].Location);
	}

	OutAlongPath = Along;
	OutStraightLine = FVector::Dist2D(Position, Points.Last().Location);
	bOutFinalSegment = NextIndex >= Points.Num() - 1;
	return true;
}

EStopDirection UPredictiveStopComponent::ComputeStopDirection(const FVector& Velocity) const
{
	const FVector Local = GetOwner()->GetActorRotation().UnrotateVector(Velocity);
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));

	if (FMath::Abs(AngleDeg) <= 45.f)
	{
		return EStopDirection::Forward;
	}
	if (FMath::Abs(AngleDeg) >= 135.f)
	{
		return EStopDirection::Backward;
	}
	return AngleDeg > 0.f ? EStopDirection::Right : EStopDirection::Left;
}

int32 UPredictiveStopComponent::SelectStopEntry(EStopDirection Direction, float Speed, float RemainingAlongPath, float DistToNextPoint, bool bOnFinalSegment, float StraightLineToGoal, float Lookahead) const
{
	// A meaningful corner sits between us and the goal when the along-path and
	// straight-line distances disagree. A stop sliding across that corner cuts off-path
	// (potentially off-navmesh -- exactly the geometry this system exists to protect),
	// so while corner-blocked a candidate must fit inside the CURRENT segment.
	const bool bCornerBlocked = !bOnFinalSegment && (RemainingAlongPath - StraightLineToGoal) > CornerCutTolerance;

	// Best entry of one tier: direction match preferred, then closest reference speed.
	// bMustFit: authored distance must fit in the remaining distance (+ trigger slack).
	auto PickBest = [&](bool bQuickTier, bool bMustFit) -> int32
	{
		int32 Best = INDEX_NONE;
		float BestScore = TNumericLimits<float>::Max();
		int32 BestAnyDirection = INDEX_NONE;
		float BestAnyDirectionScore = TNumericLimits<float>::Max();

		for (int32 i = 0; i < RuntimeEntries.Num(); ++i)
		{
			const FRuntimeStopEntry& Runtime = RuntimeEntries[i];
			if (Runtime.Entry.bQuickStop != bQuickTier)
			{
				continue;
			}

			const float StopDistance = Runtime.Curve.TotalDistance;
			if (bMustFit && StopDistance > RemainingAlongPath + Lookahead)
			{
				continue;
			}
			if (bCornerBlocked && StopDistance > DistToNextPoint)
			{
				continue;
			}

			const float Score = FMath::Abs(Runtime.Entry.ReferenceSpeed - Speed);
			if (Runtime.Entry.Direction == Direction)
			{
				if (Score < BestScore) { Best = i; BestScore = Score; }
			}
			else if (Score < BestAnyDirectionScore)
			{
				BestAnyDirection = i; BestAnyDirectionScore = Score;
			}
		}
		return Best != INDEX_NONE ? Best : BestAnyDirection;
	};

	// Tier 1: directional stop that fits (skipped when the plan already ruled it out).
	if (CurrentPlan == EStopPlan::DirectionalStop)
	{
		const int32 Directional = PickBest(/*bQuickTier*/ false, /*bMustFit*/ true);
		if (Directional != INDEX_NONE)
		{
			return Directional;
		}
	}

	// Tier 2: quick stop that fits.
	const int32 Quick = PickBest(/*bQuickTier*/ true, /*bMustFit*/ true);
	if (Quick != INDEX_NONE)
	{
		return Quick;
	}

	// Tier 3: late-trigger recovery. Remaining distance is already INSIDE every stop's
	// authored distance (repath mid-run, move issued at close range). Take the
	// smallest-distance entry available -- the stop will start mid-animation at the
	// distance-matched time so its remaining slide equals the actual remaining distance.
	// While corner-blocked we defer instead: after the corner this runs again.
	if (!bCornerBlocked)
	{
		int32 Smallest = INDEX_NONE;
		float SmallestDistance = TNumericLimits<float>::Max();
		for (int32 i = 0; i < RuntimeEntries.Num(); ++i)
		{
			// Prefer quick entries for recovery; directional only if no quick exists.
			const float Bias = RuntimeEntries[i].Entry.bQuickStop ? 0.f : 1000000.f;
			const float StopDistance = RuntimeEntries[i].Curve.TotalDistance + Bias;
			if (StopDistance < SmallestDistance)
			{
				Smallest = i;
				SmallestDistance = StopDistance;
			}
		}
		if (Smallest != INDEX_NONE && RuntimeEntries[Smallest].Curve.TotalDistance > RemainingAlongPath)
		{
			return Smallest;
		}
	}

	return INDEX_NONE;
}

// --------------------------------------------------------------------------- stopping --

void UPredictiveStopComponent::TriggerStop(int32 EntryIndex, float RemainingDistance, UPathFollowingComponent& PathFollowing)
{
	const FRuntimeStopEntry& Runtime = RuntimeEntries[EntryIndex];

	ActiveEntryIndex = EntryIndex;
	ActiveStopAnim = Runtime.Entry.StopAnim;
	ActiveStopDirection = Runtime.Entry.Direction;
	ActiveCurve = Runtime.Curve;

	StopTargetLocation = PathFollowing.GetPath()->GetPathPoints().Last().Location;
	DistanceToStopTarget = RemainingDistance;

	// Matched start time: 0 for a normal trigger (remaining >= authored distance); mid-anim
	// for late-trigger recovery so remaining root motion == actual remaining distance.
	MatchedStopTime = Runtime.Curve.GetTimeForRemainingDistance(RemainingDistance);

	StopElapsed = 0.f;
	StopTimeout = (Runtime.Curve.PlayLength - MatchedStopTime) * 1.5f + CompletionTimeoutSlack;

	if (bPauseMoveOnTrigger)
	{
		PausedRequestId = PathFollowing.GetCurrentRequestId();
		PathFollowing.PauseMove(PausedRequestId, EPathFollowingVelocityMode::Reset);
	}

	State = EPredictiveStopState::Stopping;
	bStopActive = true;

	UE_LOG(LogPredictiveStop, Verbose, TEXT("%s: stop triggered -- '%s', remaining %.1f cm, authored %.1f cm, start time %.3f s."),
		*GetNameSafe(GetOwner()), *GetNameSafe(ActiveStopAnim), RemainingDistance, Runtime.Curve.TotalDistance, MatchedStopTime);

	OnStopTriggered.Broadcast(EntryIndex, Runtime.Entry.GaitName, StopTargetLocation, MatchedStopTime);
}

void UPredictiveStopComponent::UpdateStopping(float DeltaTime)
{
	StopElapsed += DeltaTime;

	const FVector Position = GetOwner()->GetActorLocation();
	FVector ToTarget = StopTargetLocation - Position;
	ToTarget.Z = 0.f;
	float Distance = ToTarget.Size();

	// Reached-or-passed detection: once the target is behind our motion, clamp the
	// remaining distance to zero so matched time runs the anim out instead of chasing a
	// re-growing straight-line distance.
	const FVector Velocity = GetOwner()->GetVelocity();
	if (Distance < 1.f ||
		(Velocity.SizeSquared2D() > 1.f && FVector::DotProduct(ToTarget.GetSafeNormal(), Velocity.GetSafeNormal2D()) < 0.f))
	{
		Distance = 0.f;
	}

	DistanceToStopTarget = Distance;
	MatchedStopTime = UAdvancedDistanceMatchingLibrary::AdvanceTimeClamped(ActiveCurve, MatchedStopTime, Distance, DeltaTime, PlayRateClamp);

	// Safety net: never leave the AI parked in Stopping if the finished-notify was lost
	// (anim interrupted, notify missing on the asset, LOD-ed out mesh, ...).
	if (StopElapsed >= StopTimeout)
	{
		UE_LOG(LogPredictiveStop, Warning, TEXT("%s: stop timed out after %.2f s -- missing 'Predictive Stop Finished' notify on '%s'?"),
			*GetNameSafe(GetOwner()), StopElapsed, *GetNameSafe(ActiveStopAnim));
		CompleteStop();
	}
}

void UPredictiveStopComponent::NotifyStopFinished()
{
	if (State == EPredictiveStopState::Stopping)
	{
		CompleteStop();
	}
}

void UPredictiveStopComponent::CompleteStop()
{
	bStopActive = false;
	State = EPredictiveStopState::Inactive;

	// Resume the paused request: path following immediately sees the goal reached (we
	// stopped ON it) and finishes with success, so BT MoveTo completes naturally. If the
	// landing is a hair outside the acceptance radius it walks the last centimeters.
	if (bResumeMoveOnComplete)
	{
		if (UPathFollowingComponent* PathFollowing = BoundPathFollowing.Get())
		{
			if (PathFollowing->GetStatus() == EPathFollowingStatus::Paused)
			{
				PathFollowing->ResumeMove(PausedRequestId);
			}
		}
	}

	OnStopCompleted.Broadcast();
	ResetStopOutputs();
}

void UPredictiveStopComponent::CancelStop()
{
	if (State != EPredictiveStopState::Stopping)
	{
		return;
	}

	bStopActive = false;
	State = EPredictiveStopState::Inactive;
	OnStopCanceled.Broadcast();
	ResetStopOutputs();
}

void UPredictiveStopComponent::ResetStopOutputs()
{
	ActiveEntryIndex = INDEX_NONE;
	ActiveStopAnim = nullptr;
	ActiveCurve = FDistanceCurveCache();
	DistanceToStopTarget = 0.f;
	MatchedStopTime = 0.f;
	StopElapsed = 0.f;
}

// --------------------------------------------------------------------------- planning --

void UPredictiveStopComponent::EvaluateMovePlan(float TotalPathLength)
{
	TArray<FGaitPlanInfo> Gaits;
	BuildGaitTable(Gaits);

	CurrentPlan = EStopPlan::IdleOrStep;
	RecommendedGaitName = Gaits.Num() > 0 ? Gaits.Last().GaitName : NAME_None; // slowest as default

	// Fastest gait whose full directional stop (plus commit margin for acceleration and
	// trigger slack) fits inside the path -- the paper algorithm's sub-case, generalized
	// from "directional vs idle" to a full gait ladder.
	for (const FGaitPlanInfo& Gait : Gaits)
	{
		if (TotalPathLength >= Gait.StopDistance + GaitCommitMargin)
		{
			CurrentPlan = EStopPlan::DirectionalStop;
			RecommendedGaitName = Gait.GaitName;
			break;
		}
	}

	if (CurrentPlan == EStopPlan::IdleOrStep)
	{
		const float MinQuickDistance = GetMinQuickStopDistance();
		if (MinQuickDistance > 0.f && TotalPathLength >= MinQuickDistance)
		{
			CurrentPlan = EStopPlan::QuickStop;
		}
	}

	UE_LOG(LogPredictiveStop, Verbose, TEXT("%s: move plan -- path %.1f cm, plan %s, gait %s."),
		*GetNameSafe(GetOwner()), TotalPathLength, *UEnum::GetValueAsString(CurrentPlan), *RecommendedGaitName.ToString());

	OnMovePlanEvaluated.Broadcast(CurrentPlan, RecommendedGaitName, TotalPathLength);
}

void UPredictiveStopComponent::BuildGaitTable(TArray<FGaitPlanInfo>& OutGaits) const
{
	for (const FRuntimeStopEntry& Runtime : RuntimeEntries)
	{
		if (Runtime.Entry.bQuickStop)
		{
			continue;
		}

		FGaitPlanInfo* Found = OutGaits.FindByPredicate([&](const FGaitPlanInfo& G) { return G.GaitName == Runtime.Entry.GaitName; });
		if (!Found)
		{
			Found = &OutGaits.AddDefaulted_GetRef();
			Found->GaitName = Runtime.Entry.GaitName;
		}
		Found->ReferenceSpeed = FMath::Max(Found->ReferenceSpeed, Runtime.Entry.ReferenceSpeed);
		// Max over the gait's directions: plan against the worst case so any direction fits.
		Found->StopDistance = FMath::Max(Found->StopDistance, Runtime.Curve.TotalDistance);
	}

	OutGaits.Sort([](const FGaitPlanInfo& A, const FGaitPlanInfo& B) { return A.ReferenceSpeed > B.ReferenceSpeed; });
}

float UPredictiveStopComponent::GetMinQuickStopDistance() const
{
	float MinDistance = 0.f;
	for (const FRuntimeStopEntry& Runtime : RuntimeEntries)
	{
		if (Runtime.Entry.bQuickStop && (MinDistance <= 0.f || Runtime.Curve.TotalDistance < MinDistance))
		{
			MinDistance = Runtime.Curve.TotalDistance;
		}
	}
	return MinDistance;
}

float UPredictiveStopComponent::GetStopDistanceForGait(FName GaitName) const
{
	TArray<FGaitPlanInfo> Gaits;
	BuildGaitTable(Gaits);
	const FGaitPlanInfo* Found = Gaits.FindByPredicate([&](const FGaitPlanInfo& G) { return G.GaitName == GaitName; });
	return Found ? Found->StopDistance : 0.f;
}

FName UPredictiveStopComponent::GetMaxGaitForPathLength(float PathLength) const
{
	TArray<FGaitPlanInfo> Gaits;
	BuildGaitTable(Gaits);
	for (const FGaitPlanInfo& Gait : Gaits)
	{
		if (PathLength >= Gait.StopDistance + GaitCommitMargin)
		{
			return Gait.GaitName;
		}
	}
	return NAME_None;
}

float UPredictiveStopComponent::GetMatchedTimeForDistance(float Distance) const
{
	return ActiveCurve.IsValid() ? ActiveCurve.GetTimeForRemainingDistance(Distance) : 0.f;
}

float UPredictiveStopComponent::PredictBrakingStopDistance() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return 0.f;
	}

	return UAdvancedDistanceMatchingLibrary::PredictGroundMovementStopDistance(
		Movement->Velocity,
		Movement->bUseSeparateBrakingFriction,
		Movement->GroundFriction,
		Movement->BrakingFriction,
		Movement->BrakingFrictionFactor,
		Movement->BrakingDecelerationWalking);
}

// --------------------------------------------------------------------------------- ai --

UPathFollowingComponent* UPredictiveStopComponent::ResolvePathFollowing()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	UPathFollowingComponent* PathFollowing = Controller ? Controller->GetPathFollowingComponent() : nullptr;

	// Possession can change at runtime; keep the finished-callback bound to the live one.
	if (PathFollowing != BoundPathFollowing.Get())
	{
		if (UPathFollowingComponent* Old = BoundPathFollowing.Get())
		{
			Old->OnRequestFinished.Remove(RequestFinishedHandle);
		}
		BoundPathFollowing = PathFollowing;
		if (PathFollowing)
		{
			RequestFinishedHandle = PathFollowing->OnRequestFinished.AddUObject(this, &UPredictiveStopComponent::HandleMoveRequestFinished);
		}
	}

	return PathFollowing;
}

void UPredictiveStopComponent::HandleMoveRequestFinished(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	// Our own PauseMove never lands here (pause is not a finish). This fires when the
	// request truly ends: success after our resume, or an external abort (BT branch
	// change, new move request) -- in which case the stop must release the character.
	if (State == EPredictiveStopState::Stopping && !Result.IsSuccess() && bCancelStopOnMoveAborted)
	{
		UE_LOG(LogPredictiveStop, Verbose, TEXT("%s: move request finished (%s) mid-stop -- canceling stop."),
			*GetNameSafe(GetOwner()), *Result.ToString());
		CancelStop();
	}
}

// ------------------------------------------------------------------------------ debug --

void UPredictiveStopComponent::DrawDebugInfo() const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector OwnerLocation = GetOwner()->GetActorLocation();

	if (State == EPredictiveStopState::Monitoring && LastCandidateIndex != INDEX_NONE)
	{
		// Goal + the authored stop distance ring around it: the ring is the trigger line.
		DrawDebugSphere(World, CachedGoal, 15.f, 12, FColor::Yellow, false, -1.f, 0, 1.5f);
		DrawDebugCircle(World, CachedGoal + FVector(0, 0, 5), LastCandidateStopDistance, 48, FColor::Cyan,
			false, -1.f, 0, 1.5f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}
	else if (State == EPredictiveStopState::Stopping)
	{
		DrawDebugSphere(World, StopTargetLocation, 15.f, 12, FColor::Green, false, -1.f, 0, 1.5f);
		DrawDebugLine(World, OwnerLocation, StopTargetLocation, FColor::Green, false, -1.f, 0, 2.f);
		DrawDebugString(World, OwnerLocation + FVector(0, 0, 120),
			FString::Printf(TEXT("STOP %.0fcm t=%.2f"), DistanceToStopTarget, MatchedStopTime),
			nullptr, FColor::White, 0.f, true);
	}
#endif
}

// ------------------------------------------------------------------------ anim notify --

void UAnimNotify_PredictiveStopFinished::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (UPredictiveStopComponent* Component = Owner ? Owner->FindComponentByClass<UPredictiveStopComponent>() : nullptr)
	{
		Component->NotifyStopFinished();
	}
}
