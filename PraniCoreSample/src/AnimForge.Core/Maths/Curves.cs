using System.Numerics;

namespace AnimForge.Core.Maths;

/// <summary>Small curve/easing toolbox shared by tools and runtime code.</summary>
public static class Curves
{
    public static float SmoothStep01(float t)
    {
        t = Math.Clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    public static float EaseInOutCubic(float t)
    {
        t = Math.Clamp(t, 0.0f, 1.0f);
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - MathF.Pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    }

    public static float Remap(float v, float inMin, float inMax, float outMin, float outMax)
    {
        float u = MathF.Abs(inMax - inMin) < 1e-8f ? 0.0f : (v - inMin) / (inMax - inMin);
        return outMin + Math.Clamp(u, 0.0f, 1.0f) * (outMax - outMin);
    }

    /// <summary>Cubic Hermite between p0 (tangent m0) and p1 (tangent m1), t in [0,1].</summary>
    public static float Hermite(float p0, float m0, float p1, float m1, float t)
    {
        float t2 = t * t, t3 = t2 * t;
        return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0
             + (t3 - 2.0f * t2 + t) * m0
             + (-2.0f * t3 + 3.0f * t2) * p1
             + (t3 - t2) * m1;
    }

    /// <summary>Centripetal-ish Catmull-Rom through p1..p2 with neighbors p0/p3, t in [0,1].</summary>
    public static Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
    {
        float t2 = t * t, t3 = t2 * t;
        return 0.5f * ((2.0f * p1)
            + (-p0 + p2) * t
            + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
            + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    }
}
