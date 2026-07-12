using System.Collections.Concurrent;
using System.Diagnostics;
using ImGuiNET;
using Raylib_cs;
using Prani.Engine.Import;
using Prani.Engine.Render;
using Prani.Engine.Scene;
using Prani.Engine.UI;
using Prani.Engine.UI.Panels;
using Prani.Engine.Viewport;

namespace Prani.Engine;

/// <summary>
/// Owns the raylib window + render thread. Everything GL/scene-related lives here.
///
/// Threading contract (see docs/08_Threading_And_Commands.md):
///  * Start() spawns ONE dedicated thread; raylib init, the frame loop, and shutdown
///    all happen on it (GL contexts are thread-affine).
///  * Other threads talk to the engine exclusively via Enqueue(IEngineCommand).
///  * The engine talks back via SceneChanged/Stopped events, raised on the render
///    thread — subscribers (Avalonia) must marshal to their own thread.
/// </summary>
public sealed class RenderHost
{
    private readonly ConcurrentQueue<IEngineCommand> _commands = new();
    private readonly ManualResetEventSlim _windowReady = new(false);
    private Thread? _thread;
    private volatile bool _stopRequested;

    private readonly Scene3D _scene = new();
    private readonly SceneRenderer _renderer = new();
    private ViewportManager? _viewports;
    private ImGuiLayer? _imgui;

    public bool IsRunning => _thread is { IsAlive: true };

    /// <summary>
    /// Embedded mode: the window is created borderless + hidden so a host UI (Avalonia)
    /// can reparent it as a native child control instead of it living on the desktop.
    /// </summary>
    public bool Embedded { get; private set; }

    /// <summary>Native handle (HWND on Windows) of the raylib window, valid once WaitForWindow returns.</summary>
    public IntPtr WindowHandle { get; private set; }

    /// <summary>Blocks until the render thread has created the window (or timeout).</summary>
    public bool WaitForWindow(TimeSpan timeout) => _windowReady.Wait(timeout);

    /// <summary>Raised after any scene mutation, with a UI-safe snapshot. Render-thread callback!</summary>
    public event Action<SceneSnapshot>? SceneChanged;
    /// <summary>Raised when the render loop has fully shut down. Render-thread callback!</summary>
    public event Action? Stopped;

    public void Enqueue(IEngineCommand command) => _commands.Enqueue(command);

    public void Start(bool embedded = false)
    {
        if (IsRunning) return;
        Embedded = embedded;
        _stopRequested = false;
        _thread = new Thread(RunLoop) { Name = "Prani.RenderThread", IsBackground = false };
        _thread.Start();
    }

    /// <summary>Request shutdown and block until the render thread exits.</summary>
    public void Stop(TimeSpan? timeout = null)
    {
        if (!IsRunning) return;
        _stopRequested = true;
        _thread!.Join(timeout ?? TimeSpan.FromSeconds(5));
    }

    // ------------------------------------------------------------------ render thread

    private void RunLoop()
    {
        try
        {
            RunLoopCore();
        }
        catch (Exception ex)
        {
            // A crash on this thread would otherwise take the process down with no trace.
            CrashLog.Write("RenderThread", ex);
            Log.Error($"Render thread crashed: {ex.Message} (see prani-crash.log)");
            try { if (Raylib.IsWindowReady()) Raylib.CloseWindow(); } catch { /* teardown best-effort */ }
            Stopped?.Invoke();
        }
    }

    private void RunLoopCore()
    {
        Raylib.SetTraceLogLevel(TraceLogLevel.Warning);
        var flags = ConfigFlags.Msaa4xHint | ConfigFlags.ResizableWindow | ConfigFlags.VSyncHint;
        if (Embedded)
        {
            // Borderless + hidden: the Avalonia shell reparents this window into itself
            // (RaylibHost control) and shows it once it is a child.
            flags |= ConfigFlags.UndecoratedWindow | ConfigFlags.HiddenWindow;
        }
        Raylib.SetConfigFlags(flags);
        Raylib.InitWindow(1600, 900, "Prani — Workspace");
        Raylib.SetExitKey(KeyboardKey.Null); // ESC must not kill the tool

        unsafe { WindowHandle = (IntPtr)Raylib.GetWindowHandle(); }
        _windowReady.Set();

        _viewports = new ViewportManager();
        _imgui = new ImGuiLayer();
        _imgui.Setup();

        var ctx = new EngineContext
        {
            Scene = _scene,
            Enqueue = Enqueue,
            Renderer = _renderer,
            Viewports = _viewports,
        };

        Log.Info("Prani workspace ready. File > Import to load an FBX.");
        PublishSnapshot();

        var sw = new Stopwatch();
        while (!_stopRequested && !Raylib.WindowShouldClose())
        {
            sw.Restart();

            DrainCommands();
            if (_stopRequested) break;

            // As an embedded child window we must claim keyboard focus on click ourselves
            // (Win32 only activates top-level windows on mouse-down).
            if (Embedded && OperatingSystem.IsWindows() && !Raylib.IsWindowFocused()
                && (Raylib.IsMouseButtonPressed(MouseButton.Left)
                    || Raylib.IsMouseButtonPressed(MouseButton.Middle)
                    || Raylib.IsMouseButtonPressed(MouseButton.Right)))
            {
                Win32.SetFocus(WindowHandle);
            }

            // 1) 3D content into each viewport's render texture.
            _viewports.RenderAll((cam, vp) => _renderer.Draw(_scene, cam, vp));

            // 2) Compose the frame: ImGui dockspace hosts the viewport images + panels.
            Raylib.BeginDrawing();
            Raylib.ClearBackground(new Color(18, 18, 20, 255));
            _imgui.DrawFrame(ctx, () => _viewports.DrawAllImGui(), DrawMainMenuBar);
            Raylib.EndDrawing();

            ctx.FrameTimeMs = (float)sw.Elapsed.TotalMilliseconds;
        }

        // Teardown on the same thread.
        _scene.Clear();
        _imgui.Dispose();
        _viewports.Dispose();
        Raylib.CloseWindow();
        Log.Info("Render thread stopped.");
        Stopped?.Invoke();
    }

    private void DrawMainMenuBar(EngineContext ctx)
    {
        // Minimal in-workspace menu; the full File menu lives in the Avalonia shell.
        if (ImGui.BeginMainMenuBar())
        {
            if (ImGui.BeginMenu("Panels"))
            {
                foreach (var p in _imgui!.Panels)
                    ImGui.MenuItem(p.Title, string.Empty, ref p.Open);
                ImGui.Separator();
                foreach (var vp in _viewports!.Viewports)
                    ImGui.MenuItem(vp.Title, string.Empty, ref vp.Open);
                ImGui.EndMenu();
            }
            ImGui.EndMainMenuBar();
        }
    }

    private void DrainCommands()
    {
        bool mutated = false;
        while (_commands.TryDequeue(out var cmd))
        {
            try
            {
                mutated |= Apply(cmd);
            }
            catch (Exception ex)
            {
                Log.Error($"{cmd.GetType().Name} failed: {ex.Message}");
            }
        }
        if (mutated) PublishSnapshot();
    }

    private bool Apply(IEngineCommand cmd)
    {
        switch (cmd)
        {
            case NewSceneCommand:
                _scene.Clear();
                Log.Info("New scene.");
                return true;

            case OpenSceneCommand c:
                SceneSerializer.Load(_scene, c.Path, LoadNodeModel);
                Log.Info($"Opened '{Path.GetFileName(c.Path)}' ({_scene.Nodes.Count} nodes).");
                FramePerspectiveOnScene();
                return true;

            case SaveSceneCommand c:
                SceneSerializer.Save(_scene, c.Path);
                Log.Info($"Saved '{Path.GetFileName(c.Path)}'.");
                return true;

            case ImportModelCommand c:
            {
                var node = _scene.AddNode(Path.GetFileNameWithoutExtension(c.Path), c.Path);
                LoadNodeModel(node);
                _scene.SelectedId = node.Id;
                FramePerspectiveOnScene();
                return true;
            }

            case RemoveNodeCommand c:
                _scene.RemoveNode(c.NodeId);
                return true;

            case SelectNodeCommand c:
                _scene.SelectedId = c.NodeId;
                return true;

            case SetNodeTransformCommand c:
            {
                var node = _scene.Find(c.NodeId);
                if (node is null) return false;
                node.Position = c.Position;
                node.RotationDeg = c.RotationDeg;
                node.Scale = c.Scale;
                _scene.Dirty = true;
                return true;
            }

            case SetNodeVisibleCommand c:
            {
                var node = _scene.Find(c.NodeId);
                if (node is null) return false;
                node.Visible = c.Visible;
                _scene.Dirty = true;
                return true;
            }

            case ShutdownCommand:
                _stopRequested = true;
                return false;

            default:
                Log.Warn($"Unhandled command {cmd.GetType().Name}.");
                return false;
        }
    }

    private static void LoadNodeModel(SceneNode node)
    {
        try
        {
            node.Model = FbxImporter.Import(node.SourcePath);
        }
        catch (Exception ex)
        {
            Log.Error($"Import failed for '{node.SourcePath}': {ex.Message}");
        }
    }

    private void FramePerspectiveOnScene()
    {
        if (_viewports is null || _scene.Nodes.Count == 0) return;
        var node = _scene.Selected ?? _scene.Nodes[^1];
        if (node.Model is null) return;

        var b = node.Model.Bounds;
        var center = System.Numerics.Vector3.Transform((b.Min + b.Max) * 0.5f, node.WorldMatrix());
        float radius = System.Numerics.Vector3.Distance(b.Min, b.Max) * 0.5f * MathF.Max(node.Scale.X, 1e-3f);
        _viewports.Perspective.Orbit.Frame(center, MathF.Max(radius, 0.5f));
    }

    private void PublishSnapshot() => SceneChanged?.Invoke(SceneSnapshot.Capture(_scene));

    private static class Win32
    {
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        public static extern IntPtr SetFocus(IntPtr hWnd);
    }
}
