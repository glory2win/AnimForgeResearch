// AnimForgeWarpVizShared - SkewWarpMath.h
//
// First-principles root-motion warping used by the gym evaluator.
// See AnimForgeLiveLink/THEORY.md for the full derivation.
//
// Core idea (SkewWarp): given the original root-motion displacement D over the
// warp window and the required displacement T (to the warp target), find the
// *minimal-change* linear map W with W * D = T:
//
//     W = I + (T - D) * D^T / (D . D)
//
// This is a rank-one update of identity - a shear (skew). Any component of
// the trajectory perpendicular to D is preserved exactly, so the "shape" of
// the motion (foot weaving, hip sway) survives while the net displacement is
// redirected onto the target. The endpoint lands on the target exactly.
//
// Rotation is corrected by distributing q_corr = q_target * q_end^-1 across
// the window, keyed to arc-length progress so the character turns while it
// travels rather than on a fixed clock.
//
// Header-only, plain C++14. Deterministic: same input -> same output on both
// the Unreal side (evaluation) and the Python mock server (Scripts/
// warpviz_skewwarp.py mirrors these formulas 1:1).

#pragma once

#include "WarpVizTypes.h"

#include <algorithm>
#include <vector>

namespace AnimForge
{
namespace WarpViz
{

// Builds the shear matrix W = I + (T - D) D^T / (D . D).
// Precondition: |D| > Epsilon (caller handles the degenerate case).
inline Mat3 ComputeSkewWarpMatrix(const Vec3& OriginalDelta, const Vec3& TargetDelta)
{
    const double DdotD = OriginalDelta.LengthSquared();
    Mat3 W = Mat3::IdentityMatrix();
    if (DdotD < 1e-12)
    {
        return W;
    }

    const Vec3 Correction = TargetDelta - OriginalDelta;
    const double Inv = 1.0 / DdotD;
    const double D[3] = { OriginalDelta.X, OriginalDelta.Y, OriginalDelta.Z };
    const double C[3] = { Correction.X, Correction.Y, Correction.Z };
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Col = 0; Col < 3; ++Col)
        {
            W.M[Row][Col] += C[Row] * D[Col] * Inv;
        }
    }
    return W;
}

struct WarpTrajectoryParams
{
    double WindowStartSeconds = 0.0;
    double WindowEndSeconds = 0.0;
    Vec3 TargetTranslation;                 // world/component space, same space as samples
    Quat TargetRotation = Quat::Identity();
    WarpMethod Method = WarpMethod::SkewWarp;
    bool bWarpRotation = true;
    bool bWarpTranslation = true;
};

struct WarpTrajectoryResult
{
    std::vector<TrajectorySample> Samples;  // same times as the input samples
    bool bWindowFound = false;              // false when no samples fall inside the window
    Vec3 AchievedEndTranslation;            // warped translation at window end (== target when exact)
};

namespace Detail
{

// Arc-length progress of sample Index through [I0, I1], in [0, 1].
// Falls back to normalized time when the path is degenerate (standing still).
inline double ArcLengthAlpha(const std::vector<TrajectorySample>& Samples,
                             size_t I0, size_t I1, size_t Index)
{
    double Total = 0.0;
    double AtIndex = 0.0;
    for (size_t i = I0 + 1; i <= I1; ++i)
    {
        Total += (Samples[i].Translation - Samples[i - 1].Translation).Length();
        if (i <= Index)
        {
            AtIndex = Total;
        }
    }
    if (Total > 1e-9)
    {
        return AtIndex / Total;
    }
    const double TimeSpan = Samples[I1].TimeSeconds - Samples[I0].TimeSeconds;
    if (TimeSpan <= 1e-12)
    {
        return (Index >= I1) ? 1.0 : 0.0;
    }
    return (Samples[Index].TimeSeconds - Samples[I0].TimeSeconds) / TimeSpan;
}

} // namespace Detail

// Warps the given root trajectory so that at WindowEndSeconds the root reaches
// TargetTranslation / TargetRotation. Samples before the window are untouched;
// samples after the window are carried rigidly (rotated about the window-end
// pivot and offset) so the post-warp motion stays continuous.
inline WarpTrajectoryResult WarpTrajectory(const std::vector<TrajectorySample>& Samples,
                                           const WarpTrajectoryParams& Params)
{
    WarpTrajectoryResult Result;
    Result.Samples = Samples;
    if (Samples.empty())
    {
        return Result;
    }

    // Locate the warp window in the sample list.
    size_t I0 = Samples.size();
    size_t I1 = 0;
    for (size_t i = 0; i < Samples.size(); ++i)
    {
        const double t = Samples[i].TimeSeconds;
        if (t >= Params.WindowStartSeconds - 1e-9 && I0 == Samples.size())
        {
            I0 = i;
        }
        if (t <= Params.WindowEndSeconds + 1e-9)
        {
            I1 = i;
        }
    }
    if (I0 >= Samples.size() || I1 <= I0)
    {
        return Result; // window empty or a single sample: nothing to warp
    }
    Result.bWindowFound = true;

    const Vec3 Origin = Samples[I0].Translation;
    const Vec3 OriginalDelta = Samples[I1].Translation - Origin;
    const Vec3 TargetDelta = Params.TargetTranslation - Origin;
    const bool bDegenerate = OriginalDelta.LengthSquared() < 1e-12;

    Mat3 W = Mat3::IdentityMatrix();
    double ScaleFactor = 1.0;
    if (Params.bWarpTranslation && !bDegenerate)
    {
        switch (Params.Method)
        {
        case WarpMethod::SkewWarp:
        case WarpMethod::SimpleWarp: // SimpleWarp shares the shear map; it differs
                                     // in rotation handling (none) on the UE side.
            W = ComputeSkewWarpMatrix(OriginalDelta, TargetDelta);
            break;
        case WarpMethod::Scale:
        {
            // Pure scale along the original direction; ignores lateral error.
            const double Proj = TargetDelta.Dot(OriginalDelta) / OriginalDelta.LengthSquared();
            ScaleFactor = Proj;
            break;
        }
        default:
            break;
        }
    }

    const Quat EndRotation = Samples[I1].Rotation;
    const Quat RotationCorrection = Params.bWarpRotation
        ? (Params.TargetRotation * EndRotation.Conjugate()).Normalized()
        : Quat::Identity();

    // --- warp samples inside the window --------------------------------
    for (size_t i = I0; i <= I1; ++i)
    {
        const Vec3 Local = Samples[i].Translation - Origin;
        Vec3 WarpedLocal = Local;

        if (Params.bWarpTranslation)
        {
            if (bDegenerate)
            {
                // No original displacement to shear: ramp the target offset in
                // linearly over the window instead.
                const double Alpha = Detail::ArcLengthAlpha(Samples, I0, I1, i);
                WarpedLocal = Local + TargetDelta * Alpha;
            }
            else if (Params.Method == WarpMethod::Scale)
            {
                WarpedLocal = Local * ScaleFactor;
            }
            else
            {
                WarpedLocal = W.Transform(Local);
            }
        }

        Result.Samples[i].Translation = Origin + WarpedLocal;

        if (Params.bWarpRotation)
        {
            const double Alpha = Detail::ArcLengthAlpha(Samples, I0, I1, i);
            const Quat Partial = Quat::Slerp(Quat::Identity(), RotationCorrection, Alpha);
            Result.Samples[i].Rotation = (Partial * Samples[i].Rotation).Normalized();
        }
    }

    // --- carry samples after the window rigidly ------------------------
    const Vec3 OldPivot = Samples[I1].Translation;
    const Vec3 NewPivot = Result.Samples[I1].Translation;
    for (size_t i = I1 + 1; i < Samples.size(); ++i)
    {
        const Vec3 FromPivot = Samples[i].Translation - OldPivot;
        Result.Samples[i].Translation = NewPivot + RotationCorrection.RotateVector(FromPivot);
        Result.Samples[i].Rotation = (RotationCorrection * Samples[i].Rotation).Normalized();
    }

    Result.AchievedEndTranslation = Result.Samples[I1].Translation;
    return Result;
}

} // namespace WarpViz
} // namespace AnimForge
