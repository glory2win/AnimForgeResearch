using Prani.Engine.Scene;

namespace Prani.Engine.UI.Panels;

/// <summary>
/// Base class for dockable ImGui panels. Panels run ON the render thread and may read
/// the live scene directly; mutations still go through ctx.Enqueue so every change
/// funnels through one code path (and the UI snapshot stays in sync).
/// </summary>
public abstract class Panel
{
    public abstract string Title { get; }
    public bool Open = true;

    public abstract void Draw(EngineContext ctx);
}

/// <summary>What panels get to see each frame.</summary>
public sealed class EngineContext
{
    public required Scene3D Scene { get; init; }
    public required Action<IEngineCommand> Enqueue { get; init; }
    public required Render.SceneRenderer Renderer { get; init; }
    public required Viewport.ViewportManager Viewports { get; init; }
    public float FrameTimeMs;
}
