// AnimForgeUnrealWarpViz - SkewWarpMathTests.cpp
//
// Automation tests for the shared skew-warp math, run inside the engine so
// the exact binary the gym evaluates with is what gets tested.

#include "Misc/AutomationTest.h"

#include "SkewWarpMath.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AnimForge::WarpViz;

namespace
{

std::vector<TrajectorySample> StraightWalk(int32 Frames = 11, double Step = 10.0, double Fps = 30.0)
{
	std::vector<TrajectorySample> Samples;
	for (int32 Index = 0; Index < Frames; ++Index)
	{
		TrajectorySample Sample;
		Sample.TimeSeconds = static_cast<double>(Index) / Fps;
		Sample.Translation = Vec3(0.0, 0.0, Step * Index);
		Samples.push_back(Sample);
	}
	return Samples;
}

WarpTrajectoryParams MakeParams(const std::vector<TrajectorySample>& Samples, const Vec3& Target)
{
	WarpTrajectoryParams Params;
	Params.WindowStartSeconds = Samples.front().TimeSeconds;
	Params.WindowEndSeconds = Samples.back().TimeSeconds;
	Params.TargetTranslation = Target;
	Params.bWarpRotation = false;
	return Params;
}

} // anonymous namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkewWarpMatrixTest,
	"AnimForge.WarpViz.SkewWarp.MatrixMapsDeltaToTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FSkewWarpMatrixTest::RunTest(const FString& Parameters)
{
	const Vec3 D(3.0, -1.0, 12.0);
	const Vec3 T(5.0, 4.0, 9.0);
	const Mat3 W = ComputeSkewWarpMatrix(D, T);

	TestTrue(TEXT("W * D == T"), W.Transform(D).NearlyEquals(T, 1e-9));

	// Perpendicular components must pass through unchanged (shear property).
	const Vec3 DPure(0.0, 0.0, 10.0);
	const Mat3 WPure = ComputeSkewWarpMatrix(DPure, Vec3(4.0, 0.0, 10.0));
	const Vec3 Perp(2.5, -7.0, 0.0);
	TestTrue(TEXT("perpendicular preserved"), WPure.Transform(Perp).NearlyEquals(Perp, 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkewWarpEndpointTest,
	"AnimForge.WarpViz.SkewWarp.EndpointHitsTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FSkewWarpEndpointTest::RunTest(const FString& Parameters)
{
	const std::vector<TrajectorySample> Samples = StraightWalk();
	const Vec3 Target(50.0, 0.0, 130.0);

	const WarpTrajectoryResult Result = WarpTrajectory(Samples, MakeParams(Samples, Target));

	TestTrue(TEXT("window found"), Result.bWindowFound);
	TestTrue(TEXT("endpoint == target"),
		Result.Samples.back().Translation.NearlyEquals(Target, 1e-9));
	TestTrue(TEXT("start unchanged"),
		Result.Samples.front().Translation.NearlyEquals(Samples.front().Translation, 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkewWarpPostWindowRigidTest,
	"AnimForge.WarpViz.SkewWarp.PostWindowCarriedRigidly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FSkewWarpPostWindowRigidTest::RunTest(const FString& Parameters)
{
	const std::vector<TrajectorySample> Samples = StraightWalk(21);
	WarpTrajectoryParams Params = MakeParams(Samples, Vec3(40.0, 0.0, 120.0));
	Params.WindowEndSeconds = Samples[10].TimeSeconds;

	const WarpTrajectoryResult Result = WarpTrajectory(Samples, Params);

	// Per-frame deltas after the window must match the source exactly.
	for (int32 Index = 11; Index < 21; ++Index)
	{
		const Vec3 SourceDelta = Samples[Index].Translation - Samples[Index - 1].Translation;
		const Vec3 WarpedDelta =
			Result.Samples[Index].Translation - Result.Samples[Index - 1].Translation;
		TestTrue(TEXT("post-window delta preserved"), SourceDelta.NearlyEquals(WarpedDelta, 1e-9));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkewWarpDegenerateTest,
	"AnimForge.WarpViz.SkewWarp.DegenerateInPlaceAnim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FSkewWarpDegenerateTest::RunTest(const FString& Parameters)
{
	// In-place animation: no source displacement to shear; the offset must
	// ramp in and still land exactly on the target.
	std::vector<TrajectorySample> Samples;
	for (int32 Index = 0; Index < 11; ++Index)
	{
		TrajectorySample Sample;
		Sample.TimeSeconds = static_cast<double>(Index) / 30.0;
		Samples.push_back(Sample);
	}
	const Vec3 Target(0.0, 0.0, 60.0);

	const WarpTrajectoryResult Result = WarpTrajectory(Samples, MakeParams(Samples, Target));

	TestTrue(TEXT("endpoint == target"),
		Result.Samples.back().Translation.NearlyEquals(Target, 1e-9));
	for (size_t Index = 1; Index < Result.Samples.size(); ++Index)
	{
		TestTrue(TEXT("offset ramps monotonically"),
			Result.Samples[Index].Translation.Z >= Result.Samples[Index - 1].Translation.Z - 1e-9);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
