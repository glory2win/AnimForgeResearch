using System.Numerics;

namespace AnimForge.Core.Maths;

/// <summary>
/// Analytic two-bone IK (law-of-cosines). Position-space solver: given the three joint
/// positions of a chain (hip→knee→foot or shoulder→elbow→wrist), a target, and a pole
/// hint, returns the new mid and end positions. Rotation extraction for a specific
/// skeleton convention is left to the caller (documented in docs/07_AnimForge_Core.md).
/// </summary>
public static class TwoBoneIk
{
    public readonly record struct Chain(Vector3 Root, Vector3 Mid, Vector3 End);
    public readonly record struct Solution(Vector3 Root, Vector3 Mid, Vector3 End, bool Reached);

    public static Solution Solve(in Chain chain, Vector3 target, Vector3 poleHint)
    {
        float lenA = Vector3.Distance(chain.Root, chain.Mid);   // upper bone
        float lenB = Vector3.Distance(chain.Mid, chain.End);    // lower bone
        float maxReach = lenA + lenB;

        Vector3 toTarget = target - chain.Root;
        float dist = toTarget.Length();

        // Clamp: never fully straighten (avoids the elbow singularity) and never over-reach.
        float eps = 1e-4f;
        float clamped = Math.Clamp(dist, MathF.Abs(lenA - lenB) + eps, maxReach - eps);
        bool reached = dist <= maxReach - eps;

        Vector3 dir = dist > eps ? toTarget / dist : Vector3.UnitY;
        Vector3 effTarget = chain.Root + dir * clamped;

        // Law of cosines: distance from root to the knee's projection on the root→target axis.
        float a = (clamped * clamped + lenA * lenA - lenB * lenB) / (2.0f * clamped);
        float h2 = lenA * lenA - a * a;
        float h = h2 > 0.0f ? MathF.Sqrt(h2) : 0.0f;

        // Bend plane: root→target axis and the pole hint define it.
        Vector3 poleDir = poleHint - chain.Root;
        Vector3 bend = poleDir - dir * Vector3.Dot(poleDir, dir);
        if (bend.LengthSquared() < eps)
        {
            // Degenerate pole (on the axis) — fall back to any perpendicular.
            bend = Vector3.Cross(dir, MathF.Abs(dir.Y) < 0.99f ? Vector3.UnitY : Vector3.UnitX);
        }
        bend = Vector3.Normalize(bend);

        Vector3 mid = chain.Root + dir * a + bend * h;
        return new Solution(chain.Root, mid, effTarget, reached);
    }
}
