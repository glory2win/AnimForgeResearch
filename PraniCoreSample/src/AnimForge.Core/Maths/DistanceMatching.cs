namespace AnimForge.Core.Maths;

/// <summary>
/// Distance-matching support: a monotonic time→distance curve sampled from animation
/// root motion, and the inverse query "at which time has the character traveled D meters?".
/// This is the same primitive used by the AdvancedDistanceMatching research module.
/// </summary>
public sealed class DistanceCurve
{
    private readonly float[] _times;
    private readonly float[] _distances; // monotonically non-decreasing

    public DistanceCurve(float[] times, float[] distances)
    {
        if (times.Length != distances.Length || times.Length < 2)
            throw new ArgumentException("Need >= 2 samples with matching lengths.");
        _times = times;
        _distances = distances;
    }

    public float Duration => _times[^1];
    public float TotalDistance => _distances[^1];

    /// <summary>Build from per-frame root displacement magnitudes at a fixed frame rate.</summary>
    public static DistanceCurve FromDisplacements(ReadOnlySpan<float> perFrameDisplacement, float frameRate)
    {
        var times = new float[perFrameDisplacement.Length + 1];
        var dists = new float[perFrameDisplacement.Length + 1];
        for (int i = 0; i < perFrameDisplacement.Length; i++)
        {
            times[i + 1] = (i + 1) / frameRate;
            dists[i + 1] = dists[i] + MathF.Abs(perFrameDisplacement[i]);
        }
        return new DistanceCurve(times, dists);
    }

    /// <summary>Distance traveled at time t (linear interpolation between samples).</summary>
    public float DistanceAtTime(float t)
    {
        if (t <= _times[0]) return _distances[0];
        if (t >= _times[^1]) return _distances[^1];
        int hi = Array.BinarySearch(_times, t);
        if (hi >= 0) return _distances[hi];
        hi = ~hi;
        int lo = hi - 1;
        float u = (t - _times[lo]) / (_times[hi] - _times[lo]);
        return _distances[lo] + u * (_distances[hi] - _distances[lo]);
    }

    /// <summary>
    /// Inverse query: the time at which the traveled distance reaches d.
    /// Binary search over the monotonic distance array + linear interp. O(log n).
    /// </summary>
    public float FindTimeAtDistance(float d)
    {
        if (d <= _distances[0]) return _times[0];
        if (d >= _distances[^1]) return _times[^1];
        int lo = 0, hi = _distances.Length - 1;
        while (hi - lo > 1)
        {
            int mid = (lo + hi) / 2;
            if (_distances[mid] < d) lo = mid; else hi = mid;
        }
        float span = _distances[hi] - _distances[lo];
        float u = span > 1e-8f ? (d - _distances[lo]) / span : 0.0f;
        return _times[lo] + u * (_times[hi] - _times[lo]);
    }
}
