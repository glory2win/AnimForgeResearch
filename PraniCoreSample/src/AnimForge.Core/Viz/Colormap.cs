using System.Numerics;

namespace AnimForge.Core.Viz;

/// <summary>
/// Perceptual colormaps for debug visualization (heatmaps, weight painting, error fields).
/// Returned as linear-ish RGB in [0,1]. Viridis is the default: perceptually uniform,
/// colorblind-safe, and readable in grayscale — prefer it over Jet for anything shipped.
/// </summary>
public static class Colormap
{
    // Viridis control points (matplotlib), evenly spaced in t.
    private static readonly Vector3[] ViridisLut =
    {
        new(0.267f, 0.005f, 0.329f),
        new(0.283f, 0.141f, 0.458f),
        new(0.254f, 0.265f, 0.530f),
        new(0.207f, 0.372f, 0.553f),
        new(0.164f, 0.471f, 0.558f),
        new(0.128f, 0.567f, 0.551f),
        new(0.135f, 0.659f, 0.518f),
        new(0.267f, 0.749f, 0.441f),
        new(0.478f, 0.821f, 0.318f),
        new(0.741f, 0.873f, 0.150f),
        new(0.993f, 0.906f, 0.144f),
    };

    private static readonly Vector3[] CoolwarmLut =
    {
        new(0.230f, 0.299f, 0.754f),
        new(0.406f, 0.537f, 0.934f),
        new(0.602f, 0.731f, 0.999f),
        new(0.788f, 0.845f, 0.939f),
        new(0.930f, 0.820f, 0.761f),
        new(0.967f, 0.657f, 0.537f),
        new(0.887f, 0.413f, 0.324f),
        new(0.706f, 0.016f, 0.150f),
    };

    public static Vector3 Viridis(float t) => SampleLut(ViridisLut, t);
    public static Vector3 Coolwarm(float t) => SampleLut(CoolwarmLut, t);

    private static Vector3 SampleLut(Vector3[] lut, float t)
    {
        t = Math.Clamp(t, 0.0f, 1.0f) * (lut.Length - 1);
        int i = Math.Min((int)t, lut.Length - 2);
        return Vector3.Lerp(lut[i], lut[i + 1], t - i);
    }

    /// <summary>Pack [0,1] RGB into ABGR uint (the layout ImGui's AddRectFilled expects on little-endian).</summary>
    public static uint ToRgba(Vector3 rgb, float a = 1.0f)
    {
        uint r = (uint)(Math.Clamp(rgb.X, 0f, 1f) * 255.0f);
        uint g = (uint)(Math.Clamp(rgb.Y, 0f, 1f) * 255.0f);
        uint b = (uint)(Math.Clamp(rgb.Z, 0f, 1f) * 255.0f);
        uint aa = (uint)(Math.Clamp(a, 0f, 1f) * 255.0f);
        return (aa << 24) | (b << 16) | (g << 8) | r;
    }
}
