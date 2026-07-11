namespace AnimForge.Core.Viz;

/// <summary>
/// A dense W×H scalar field with min/max tracking — the data model behind heatmap panels.
/// Fill it from any Func (spring overshoot, foot sliding error, curvature, joint speed…)
/// and let the UI layer map normalized values through a Colormap.
/// </summary>
public sealed class HeatmapGrid
{
    public int Width { get; }
    public int Height { get; }
    public float Min { get; private set; }
    public float Max { get; private set; }

    private readonly float[] _cells;

    public HeatmapGrid(int width, int height)
    {
        Width = width;
        Height = height;
        _cells = new float[width * height];
        Min = 0.0f;
        Max = 1.0f;
    }

    public float this[int x, int y]
    {
        get => _cells[y * Width + x];
        set => _cells[y * Width + x] = value;
    }

    /// <summary>Evaluate f(u, v) over the grid with u,v in [0,1], then refresh min/max.</summary>
    public void Fill(Func<float, float, float> f)
    {
        for (int y = 0; y < Height; y++)
        {
            float v = Height > 1 ? y / (float)(Height - 1) : 0.0f;
            for (int x = 0; x < Width; x++)
            {
                float u = Width > 1 ? x / (float)(Width - 1) : 0.0f;
                _cells[y * Width + x] = f(u, v);
            }
        }
        RecomputeRange();
    }

    public void RecomputeRange()
    {
        Min = float.MaxValue;
        Max = float.MinValue;
        foreach (float c in _cells)
        {
            if (c < Min) Min = c;
            if (c > Max) Max = c;
        }
        if (Max - Min < 1e-12f) Max = Min + 1e-12f;
    }

    /// <summary>Cell value normalized to [0,1] by the current range — feed this to a Colormap.</summary>
    public float Normalized(int x, int y) => (this[x, y] - Min) / (Max - Min);
}
