using System.Numerics;
using ImGuiNET;
using Raylib_cs;
using rlImGui_cs;

namespace Prani.Engine.Viewport;

public enum ViewportKind { Perspective, Top, Front, Right }

/// <summary>
/// One dockable 3D view: an offscreen RenderTexture displayed inside an ImGui window.
/// raylib supports exactly one OS window/GL context, so "multiple viewports" are
/// multiple render targets rendered sequentially each frame — the standard approach.
/// </summary>
public sealed class Viewport3D : IDisposable
{
    public string Title { get; }
    public ViewportKind Kind { get; }
    public OrbitCamera Orbit { get; } = new();
    public bool Open = true;
    public bool Hovered { get; private set; }

    private RenderTexture2D _rt;
    private int _width = 640;
    private int _height = 360;
    private bool _rtValid;

    public Camera3D Camera;

    public Viewport3D(string title, ViewportKind kind)
    {
        Title = title;
        Kind = kind;

        Camera = new Camera3D
        {
            Position = new Vector3(6, 5, 6),
            Target = Vector3.Zero,
            Up = Vector3.UnitY,
            FovY = 50.0f,
            Projection = kind == ViewportKind.Perspective
                ? CameraProjection.Perspective
                : CameraProjection.Orthographic,
        };

        switch (kind)
        {
            case ViewportKind.Perspective:
                Orbit.YawDeg = 45.0f; Orbit.PitchDeg = -30.0f;
                break;
            case ViewportKind.Top:
                Orbit.YawDeg = 0.0f; Orbit.PitchDeg = -89.0f; Orbit.OrbitLocked = true;
                break;
            case ViewportKind.Front:
                Orbit.YawDeg = 0.0f; Orbit.PitchDeg = 0.0f; Orbit.OrbitLocked = true;
                break;
            case ViewportKind.Right:
                Orbit.YawDeg = 90.0f; Orbit.PitchDeg = 0.0f; Orbit.OrbitLocked = true;
                break;
        }
        // In orthographic mode raylib interprets FovY as the vertical world-space size.
        if (kind != ViewportKind.Perspective) Camera.FovY = 12.0f;
    }

    /// <summary>Render the 3D content into this viewport's texture. Call between frames' logic and ImGui.</summary>
    public void RenderContent(Action<Camera3D, Viewport3D> drawScene)
    {
        EnsureRenderTexture();
        Orbit.Apply(ref Camera);
        if (Kind != ViewportKind.Perspective)
            Camera.FovY = Orbit.Distance * 1.5f; // ortho zoom follows dolly distance

        Raylib.BeginTextureMode(_rt);
        Raylib.ClearBackground(new Color(28, 30, 34, 255));
        Raylib.BeginMode3D(Camera);
        drawScene(Camera, this);
        Raylib.EndMode3D();
        Raylib.EndTextureMode();
    }

    /// <summary>Draw the ImGui window that hosts this viewport and route camera input.</summary>
    public void DrawImGuiWindow()
    {
        if (!Open) return;

        ImGui.PushStyleVar(ImGuiStyleVar.WindowPadding, new Vector2(1, 1));
        bool visible = ImGui.Begin(Title, ref Open);
        ImGui.PopStyleVar();

        if (visible)
        {
            Vector2 avail = ImGui.GetContentRegionAvail();
            _width = Math.Max(64, (int)avail.X);
            _height = Math.Max(64, (int)avail.Y);

            if (_rtValid)
                rlImGui.ImageRenderTextureFit(_rt, true);

            // Route input only when the mouse is over the image and the window has focus-ish state.
            Hovered = ImGui.IsItemHovered() || (ImGui.IsWindowHovered() && ImGui.IsWindowFocused());
            if (Hovered)
                Orbit.HandleInput();
        }
        else
        {
            Hovered = false;
        }
        ImGui.End();
    }

    private void EnsureRenderTexture()
    {
        if (_rtValid && _rt.Texture.Width == _width && _rt.Texture.Height == _height)
            return;
        if (_rtValid)
            Raylib.UnloadRenderTexture(_rt);
        _rt = Raylib.LoadRenderTexture(_width, _height);
        Raylib.SetTextureFilter(_rt.Texture, TextureFilter.Bilinear);
        _rtValid = true;
    }

    public void Dispose()
    {
        if (_rtValid)
        {
            Raylib.UnloadRenderTexture(_rt);
            _rtValid = false;
        }
    }
}
