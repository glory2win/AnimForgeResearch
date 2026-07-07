// AnimForgeUnrealWarpViz - WarpVizEvaluator.h
//
// Turns an EvaluateRequest into an EvaluateResult:
//
//   1. resolve the clip via UWarpVizSettings,
//   2. extract the clip's root motion over the requested range,
//   3. validate it against the Maya root samples (drift warning),
//   4. run the shared skew-warp math (SkewWarpMath.h - the same translation
//      shear MotionWarping's SkewWarp applies at runtime),
//   5. sample ghost poses at the requested stride.
//
// Runs on the game thread (asset access + anim evaluation are not
// thread-safe); the server marshals requests here via AsyncTask.

#pragma once

#include "CoreMinimal.h"

#include "WarpVizProtocol.h"

class UAnimSequenceBase;

namespace AnimForge
{
namespace WarpViz
{

class ANIMFORGEUNREALWARPVIZ_API FWarpVizEvaluator
{
public:
	/** Full evaluation using the project's WarpVizSettings clip registry. */
	static EvaluateResult Evaluate(const EvaluateRequest& Request);

	// --- pieces exposed for automation tests -----------------------------

	/** Extracts root-motion samples (component space, one per frame). */
	static bool ExtractRootTrajectory(UAnimSequenceBase* Animation,
	                                  const TimeRange& Range,
	                                  std::vector<TrajectorySample>& OutSamples,
	                                  FString& OutError);

	/** Max translation drift (cm) between two equally-sampled trajectories. */
	static double MeasureRootDrift(const std::vector<TrajectorySample>& MayaSamples,
	                               const std::vector<TrajectorySample>& EngineSamples);

	/** Applies the requested warp to already-extracted samples. */
	static std::vector<TrajectorySample> ApplyWarp(const EvaluateRequest& Request,
	                                               const std::vector<TrajectorySample>& Original);

	// --- conversions ------------------------------------------------------
	static Vec3 ToShared(const FVector& V) { return Vec3(V.X, V.Y, V.Z); }
	static Quat ToShared(const FQuat& Q) { return Quat(Q.X, Q.Y, Q.Z, Q.W); }
	static FVector ToUnreal(const Vec3& V) { return FVector(V.X, V.Y, V.Z); }
	static FQuat ToUnreal(const Quat& Q) { return FQuat(Q.X, Q.Y, Q.Z, Q.W); }
};

} // namespace WarpViz
} // namespace AnimForge
