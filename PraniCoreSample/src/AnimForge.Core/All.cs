using System.Numerics;
using AnimForge.Core.Maths;
using AnimForge.Core.Viz;

namespace AnimForge.Core;

/// <summary>
/// animforge_core.all — the umbrella facade over the AnimForge math core.
///
/// Usage from tools (Prani, Maya bridge, Unreal bridge):
/// <code>
///     using static AnimForge.Core.All;
///     ...
///     DamperExact(ref pos, ref vel, goal, halflife, dt);
/// </code>
///
/// Rules for this assembly:
///  * Pure math only. No raylib, no ImGui, no Avalonia, no I/O.
///  * Everything deterministic and allocation-free on hot paths.
///  * The API mirrors the planned native animforge_core (C++) one-to-one, so the
///    managed implementation can later be swapped for P/Invoke without touching callers.
/// </summary>
public static class All
{
    public const string Version = "0.1.0";

    // ---- springs / dampers (see Maths/SpringDamper.cs) ----
    public static void DamperExact(ref float x, ref float v, float goal, float halflife, float dt)
        => SpringDamper.DamperExact(ref x, ref v, goal, halflife, dt);

    public static void DamperExact(ref Vector3 x, ref Vector3 v, Vector3 goal, float halflife, float dt)
        => SpringDamper.DamperExact(ref x, ref v, goal, halflife, dt);

    public static void SpringExact(ref float x, ref float v, float goal, float frequencyHz, float halflife, float dt)
        => SpringDamper.SpringExact(ref x, ref v, goal, frequencyHz, halflife, dt);

    // ---- IK (see Maths/TwoBoneIk.cs) ----
    public static TwoBoneIk.Solution SolveTwoBoneIk(in TwoBoneIk.Chain chain, Vector3 target, Vector3 poleHint)
        => TwoBoneIk.Solve(chain, target, poleHint);

    // ---- distance matching (see Maths/DistanceMatching.cs) ----
    public static float FindTimeAtDistance(DistanceCurve curve, float distance)
        => curve.FindTimeAtDistance(distance);

    // ---- curves / easing ----
    public static float SmoothStep01(float t) => Curves.SmoothStep01(t);
    public static float Remap(float v, float inMin, float inMax, float outMin, float outMax)
        => Curves.Remap(v, inMin, inMax, outMin, outMax);

    // ---- visualization helpers ----
    public static Vector3 Viridis(float t) => Colormap.Viridis(t);
    public static Vector3 Coolwarm(float t) => Colormap.Coolwarm(t);
    public static uint ViridisRgba(float t) => Colormap.ToRgba(Colormap.Viridis(t));
}
