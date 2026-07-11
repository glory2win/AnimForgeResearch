using Raylib_cs;

namespace Prani.Engine.Viewport;

/// <summary>Owns the four default viewports (Perspective / Top / Front / Right).</summary>
public sealed class ViewportManager : IDisposable
{
    public List<Viewport3D> Viewports { get; } = new()
    {
        new Viewport3D("Perspective", ViewportKind.Perspective),
        new Viewport3D("Top", ViewportKind.Top),
        new Viewport3D("Front", ViewportKind.Front),
        new Viewport3D("Right", ViewportKind.Right),
    };

    public Viewport3D Perspective => Viewports[0];

    public void RenderAll(Action<Camera3D, Viewport3D> drawScene)
    {
        foreach (var vp in Viewports)
            if (vp.Open)
                vp.RenderContent(drawScene);
    }

    public void DrawAllImGui()
    {
        foreach (var vp in Viewports)
            vp.DrawImGuiWindow();
    }

    public void Dispose()
    {
        foreach (var vp in Viewports) vp.Dispose();
    }
}
