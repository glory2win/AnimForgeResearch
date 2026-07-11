using System.Numerics;

namespace AnimForge.Core.Maths;

/// <summary>
/// Exact (analytic) spring-damper integrators.
///
/// Instead of stiffness/damping constants we parameterize by:
///  * halflife  — seconds for the remaining error to halve (intuitive for animators)
///  * frequency — oscillations per second for the under-damped spring
///
/// "Exact" means the closed-form solution of the ODE  x'' = -k (x - g) - c x'
/// is evaluated for the given dt, so the result is stable for ANY dt (no explicit
/// Euler blow-ups) and frame-rate independent. Full derivation lives in
/// docs/07_AnimForge_Core.md.
/// </summary>
public static class SpringDamper
{
    private const float Eps = 1e-5f;

    /// <summary>halflife → exponential decay rate y such that e^(-y*halflife) = 0.5.</summary>
    public static float HalflifeToDamping(float halflife) => (4.0f * 0.69314718f) / MathF.Max(halflife, Eps);

    /// <summary>Fast approximation of e^-x for x >= 0 (accurate enough for damping).</summary>
    public static float FastNegExp(float x) => 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

    /// <summary>
    /// Critically damped spring toward a fixed goal. The workhorse: smoothing camera targets,
    /// trajectory positions, blend weights. Never overshoots.
    /// </summary>
    public static void DamperExact(ref float x, ref float v, float goal, float halflife, float dt)
    {
        float y = HalflifeToDamping(halflife) / 2.0f;
        float j0 = x - goal;
        float j1 = v + j0 * y;
        float e = FastNegExp(y * dt);

        x = e * (j0 + j1 * dt) + goal;
        v = e * (v - j1 * y * dt);
    }

    public static void DamperExact(ref Vector3 x, ref Vector3 v, Vector3 goal, float halflife, float dt)
    {
        float y = HalflifeToDamping(halflife) / 2.0f;
        Vector3 j0 = x - goal;
        Vector3 j1 = v + j0 * y;
        float e = FastNegExp(y * dt);

        x = e * (j0 + j1 * dt) + goal;
        v = e * (v - j1 * y * dt);
    }

    /// <summary>
    /// General spring with independent frequency and halflife. Under-damped when the
    /// oscillation frequency exceeds the decay rate — this is what the Heatmap panel in
    /// Prani visualizes: overshoot as a function of (frequency, halflife).
    /// </summary>
    public static void SpringExact(ref float x, ref float v, float goal, float frequencyHz, float halflife, float dt)
    {
        float w = MathF.Max(frequencyHz, 0.0f) * 2.0f * MathF.PI; // angular frequency
        float y = HalflifeToDamping(halflife) / 2.0f;             // decay rate

        if (MathF.Abs(w * w - y * y) < Eps) // critically damped
        {
            DamperExact(ref x, ref v, goal, halflife, dt);
            return;
        }

        if (w > y) // under-damped: decaying oscillation
        {
            float wd = MathF.Sqrt(w * w - y * y);
            float j0 = x - goal;
            float j1 = (v + j0 * y) / wd;
            float e = FastNegExp(y * dt);
            float c = MathF.Cos(wd * dt);
            float s = MathF.Sin(wd * dt);

            x = e * (j0 * c + j1 * s) + goal;
            v = e * (-j0 * wd * s + j1 * wd * c - y * (j0 * c + j1 * s));
        }
        else // over-damped: two real exponentials
        {
            float d = MathF.Sqrt(y * y - w * w);
            float y0 = y - d, y1 = y + d;
            float j0 = ((x - goal) * y1 + v) / (y1 - y0);
            float j1 = (x - goal) - j0;
            float e0 = FastNegExp(y0 * dt);
            float e1 = FastNegExp(y1 * dt);

            x = j0 * e0 + j1 * e1 + goal;
            v = -y0 * j0 * e0 - y1 * j1 * e1;
        }
    }

    /// <summary>
    /// Measures the maximum overshoot ratio of SpringExact for a unit step input.
    /// Used by Prani's heatmap panel as a live example of core-math-driven tooling.
    /// Returns 0 for critically/over-damped configs, grows toward 1 as damping vanishes.
    /// </summary>
    public static float MeasureOvershoot(float frequencyHz, float halflife, float simSeconds = 3.0f, float dt = 1.0f / 120.0f)
    {
        float x = 0.0f, v = 0.0f;
        float peak = 0.0f;
        for (float t = 0.0f; t < simSeconds; t += dt)
        {
            SpringExact(ref x, ref v, 1.0f, frequencyHz, halflife, dt);
            if (x - 1.0f > peak) peak = x - 1.0f;
        }
        return peak;
    }
}
