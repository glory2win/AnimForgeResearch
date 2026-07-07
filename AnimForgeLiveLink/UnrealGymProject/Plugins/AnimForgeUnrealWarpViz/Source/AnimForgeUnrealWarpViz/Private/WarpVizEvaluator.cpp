// AnimForgeUnrealWarpViz - WarpVizEvaluator.cpp

#include "WarpVizEvaluator.h"

#include "SkewWarpMath.h"
#include "WarpVizSettings.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AnimPose.h"

namespace AnimForge
{
namespace WarpViz
{

namespace
{

// Montages evaluate root motion through their slot track; sequences directly.
FTransform ExtractRootMotionInWindow(UAnimSequenceBase* Animation, double StartTime, double EndTime)
{
	if (UAnimSequence* Sequence = Cast<UAnimSequence>(Animation))
	{
		return Sequence->ExtractRootMotionFromRange(StartTime, EndTime);
	}
	if (UAnimMontage* Montage = Cast<UAnimMontage>(Animation))
	{
		return Montage->ExtractRootMotionFromTrackRange(
			static_cast<float>(StartTime), static_cast<float>(EndTime));
	}
	return FTransform::Identity;
}

void AppendGhostPoses(UAnimSequenceBase* Animation,
                      const FWarpVizClipEntry& ClipEntry,
                      const EvaluateRequest& Request,
                      const std::vector<TrajectorySample>& Original,
                      const std::vector<TrajectorySample>& Warped,
                      EvaluateResult& InOutResult)
{
	if (Request.GhostIntervalFrames <= 0.0 || Warped.empty())
	{
		return;
	}

	const double Fps = Request.Range.Fps;
	const double IntervalSeconds = Request.GhostIntervalFrames / Fps;

	double NextGhostTime = Warped.front().TimeSeconds;
	for (size_t Index = 0; Index < Warped.size(); ++Index)
	{
		const TrajectorySample& WarpedSample = Warped[Index];
		if (WarpedSample.TimeSeconds + 1e-9 < NextGhostTime)
		{
			continue;
		}
		NextGhostTime += IntervalSeconds;

		GhostPose Pose;
		Pose.TimeSeconds = WarpedSample.TimeSeconds;

		// Root joint always ships so Maya can anchor the ghost.
		JointTransform RootJoint;
		RootJoint.JointName = "root";
		RootJoint.Translation = WarpedSample.Translation;
		RootJoint.Rotation = WarpedSample.Rotation;
		Pose.Joints.push_back(RootJoint);

		// Additional bones from the clip registry, evaluated at the source
		// time and re-rooted onto the warped root transform.
		if (ClipEntry.GhostBones.Num() > 0)
		{
			const double SourceTime =
				(Request.Range.StartFrame / Fps) + Original[Index].TimeSeconds;

			FAnimPose AnimPose;
			FAnimPoseEvaluationOptions Options;
			Options.EvaluationType = EAnimDataEvalType::Raw;
			UAnimPoseExtensions::GetAnimPoseAtTime(Animation, SourceTime, Options, AnimPose);

			const FTransform WarpedRoot(
				FWarpVizEvaluator::ToUnreal(WarpedSample.Rotation),
				FWarpVizEvaluator::ToUnreal(WarpedSample.Translation));
			const FTransform OriginalRoot(
				FWarpVizEvaluator::ToUnreal(Original[Index].Rotation),
				FWarpVizEvaluator::ToUnreal(Original[Index].Translation));

			for (const FName& BoneName : ClipEntry.GhostBones)
			{
				const FTransform BoneComponentSpace =
					UAnimPoseExtensions::GetBonePose(AnimPose, BoneName, EAnimPoseSpaces::World);

				// Re-express the bone relative to the original root, then
				// attach it to the warped root.
				const FTransform BoneRelativeToRoot =
					BoneComponentSpace.GetRelativeTransform(OriginalRoot);
				const FTransform BoneWarped = BoneRelativeToRoot * WarpedRoot;

				JointTransform Joint;
				Joint.JointName = TCHAR_TO_UTF8(*BoneName.ToString());
				Joint.Translation = FWarpVizEvaluator::ToShared(BoneWarped.GetLocation());
				Joint.Rotation = FWarpVizEvaluator::ToShared(BoneWarped.GetRotation());
				Pose.Joints.push_back(Joint);
			}
		}

		InOutResult.GhostPoses.push_back(MoveTemp(Pose));
	}
}

} // anonymous namespace

bool FWarpVizEvaluator::ExtractRootTrajectory(UAnimSequenceBase* Animation,
                                              const TimeRange& Range,
                                              std::vector<TrajectorySample>& OutSamples,
                                              FString& OutError)
{
	OutSamples.clear();
	if (!Animation)
	{
		OutError = TEXT("Animation asset is null.");
		return false;
	}
	if (Range.Fps <= 0.0 || Range.EndFrame <= Range.StartFrame)
	{
		OutError = TEXT("Invalid time range.");
		return false;
	}

	const double ClipStartTime = Range.StartFrame / Range.Fps;
	const double PlayLength = static_cast<double>(Animation->GetPlayLength());
	const int32 FrameCount = static_cast<int32>(Range.EndFrame - Range.StartFrame) + 1;

	// Accumulate per-frame root motion deltas into component-space samples.
	FTransform Accumulated = FTransform::Identity;
	OutSamples.reserve(static_cast<size_t>(FrameCount));

	for (int32 Index = 0; Index < FrameCount; ++Index)
	{
		const double FrameTime = ClipStartTime + static_cast<double>(Index) / Range.Fps;
		if (FrameTime > PlayLength + 1e-6)
		{
			OutError = FString::Printf(
				TEXT("Requested range runs past the clip (%.2fs > %.2fs). Frame range/fps mismatch ")
				TEXT("between the Maya scene and the gym asset?"), FrameTime, PlayLength);
			return false;
		}

		if (Index > 0)
		{
			const double PreviousTime = ClipStartTime + static_cast<double>(Index - 1) / Range.Fps;
			const FTransform Delta = ExtractRootMotionInWindow(Animation, PreviousTime, FrameTime);
			Accumulated = Delta * Accumulated;
		}

		TrajectorySample Sample;
		Sample.TimeSeconds = static_cast<double>(Index) / Range.Fps;
		Sample.Translation = ToShared(Accumulated.GetLocation());
		Sample.Rotation = ToShared(Accumulated.GetRotation());
		OutSamples.push_back(Sample);
	}
	return true;
}

double FWarpVizEvaluator::MeasureRootDrift(const std::vector<TrajectorySample>& MayaSamples,
                                           const std::vector<TrajectorySample>& EngineSamples)
{
	double MaxDrift = 0.0;
	const size_t Count = FMath::Min(MayaSamples.size(), EngineSamples.size());
	if (Count == 0)
	{
		return 0.0;
	}

	// Compare shapes, not absolute placement: the Maya root may sit anywhere
	// in the scene while extracted root motion starts at the origin.
	const Vec3 MayaOrigin = MayaSamples[0].Translation;
	const Vec3 EngineOrigin = EngineSamples[0].Translation;
	for (size_t Index = 0; Index < Count; ++Index)
	{
		const Vec3 MayaLocal = MayaSamples[Index].Translation - MayaOrigin;
		const Vec3 EngineLocal = EngineSamples[Index].Translation - EngineOrigin;
		MaxDrift = FMath::Max(MaxDrift, (MayaLocal - EngineLocal).Length());
	}
	return MaxDrift;
}

std::vector<TrajectorySample> FWarpVizEvaluator::ApplyWarp(
	const EvaluateRequest& Request, const std::vector<TrajectorySample>& Original)
{
	WarpTrajectoryParams Params;
	Params.WindowStartSeconds =
		(Request.WarpWindow.StartFrame - Request.Range.StartFrame) / Request.Range.Fps;
	Params.WindowEndSeconds =
		(Request.WarpWindow.EndFrame - Request.Range.StartFrame) / Request.Range.Fps;
	Params.TargetTranslation = Request.Target.Translation;
	Params.TargetRotation = Request.Target.Rotation;
	Params.Method = Request.Method;
	// SimpleWarp on the UE side warps translation only.
	Params.bWarpRotation = Request.bWarpRotation && Request.Method != WarpMethod::SimpleWarp;
	Params.bWarpTranslation = Request.bWarpTranslation;

	return WarpTrajectory(Original, Params).Samples;
}

EvaluateResult FWarpVizEvaluator::Evaluate(const EvaluateRequest& Request)
{
	const double StartedSeconds = FPlatformTime::Seconds();

	EvaluateResult Result;
	Result.RequestId = Request.RequestId;

	const UWarpVizSettings* Settings = GetDefault<UWarpVizSettings>();

	if (Settings->AcceptedCharacterIds.Num() > 0
		&& !Settings->AcceptedCharacterIds.Contains(FString(Request.CharacterId.c_str())))
	{
		Result.ErrorMessage = "Character '" + Request.CharacterId + "' is not registered in this gym.";
		return Result;
	}

	const FWarpVizClipEntry* ClipEntry = Settings->FindClip(FString(Request.ClipName.c_str()));
	if (!ClipEntry)
	{
		Result.ErrorMessage = "Clip '" + Request.ClipName
			+ "' is not registered in WarpViz settings (Project Settings > AnimForge WarpViz).";
		return Result;
	}

	UAnimSequenceBase* Animation = ClipEntry->Animation.LoadSynchronous();
	if (!Animation)
	{
		Result.ErrorMessage = "Clip '" + Request.ClipName + "' asset failed to load.";
		return Result;
	}

	// --- extract ---------------------------------------------------------
	std::vector<TrajectorySample> Original;
	FString ExtractError;
	if (!ExtractRootTrajectory(Animation, Request.Range, Original, ExtractError))
	{
		Result.ErrorMessage = TCHAR_TO_UTF8(*ExtractError);
		return Result;
	}

	// --- validate against the Maya-side samples ---------------------------
	if (!Request.MayaRootSamples.empty())
	{
		const double Drift = MeasureRootDrift(Request.MayaRootSamples, Original);
		if (Drift > Settings->RootDriftWarningThreshold)
		{
			Result.Warnings.push_back(
				"Root motion drift between Maya and the gym asset is "
				+ std::to_string(Drift) + " cm (threshold "
				+ std::to_string(Settings->RootDriftWarningThreshold)
				+ "). Re-export the clip?");
		}
	}

	// --- warp + ghosts -----------------------------------------------------
	Result.OriginalTrajectory = Original;
	Result.WarpedTrajectory = ApplyWarp(Request, Original);
	AppendGhostPoses(Animation, *ClipEntry, Request, Original, Result.WarpedTrajectory, Result);

	Result.bSuccess = true;
	Result.EvaluationMs = (FPlatformTime::Seconds() - StartedSeconds) * 1000.0;
	return Result;
}

} // namespace WarpViz
} // namespace AnimForge
