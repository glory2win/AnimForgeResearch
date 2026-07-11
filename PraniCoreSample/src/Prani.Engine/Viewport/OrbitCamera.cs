using System.Numerics;
using Raylib_cs;

namespace Prani.Engine.Viewport;

/// <summary>
/// DCC-style orbit camera (Maya-ish):
///   LMB drag   — orbit (tumble)          MMB / Shift+LMB — pan (track)
///   Wheel      — dolly                    F               — frame origin/selection
/// Ortho viewports lock yaw/pitch and only pan+zoom.
/// </summary>
public sealed class OrbitCamera
{
    public Vector3 Target = Vector3.Zero;
    public float Distance = 8.0f;
    public float YawDeg;
    public float PitchDeg = -25.0f;
    public bool OrbitLocked;          // true for Top/Front/Right viewports

    public float OrbitSpeed = 0.35f;  // deg per pixel
    public float PanSpeed = 0.0016f;  // world units per pixel per unit distance
    public float ZoomSpeed = 0.12f;

    public Vector3 Eye()
    {
        float yaw = YawDeg * MathF.PI / 180.0f;
        float pitch = Math.Clamp(PitchDeg, -89.0f, 89.0f) * MathF.PI / 180.0f;
        var dir = new Vector3(
            MathF.Cos(pitch) * MathF.Sin(yaw),
            MathF.Sin(pitch),
            MathF.Cos(pitch) * MathF.Cos(yaw));
        return Target - dir * Distance;
    }

    public void Apply(ref Camera3D camera)
    {
        camera.Position = Eye();
        camera.Target = Target;
        camera.Up = Vector3.UnitY;
    }

    /// <summary>Consume raylib input. Call only while the owning viewport is hovered.</summary>
    public void HandleInput()
    {
        Vector2 delta = Raylib.GetMouseDelta();
        bool shift = Raylib.IsKeyDown(KeyboardKey.LeftShift) || Raylib.IsKeyDown(KeyboardKey.RightShift);
        bool pan = Raylib.IsMouseButtonDown(MouseButton.Middle)
                   || (shift && Raylib.IsMouseButtonDown(MouseButton.Left));
        bool orbit = !pan && Raylib.IsMouseButtonDown(MouseButton.Left) && !OrbitLocked;

        if (orbit)
        {
            YawDeg -= delta.X * OrbitSpeed;
            PitchDeg = Math.Clamp(PitchDeg - delta.Y * OrbitSpeed, -89.0f, 89.0f);
        }
        else if (pan)
        {
            // Build camera basis to pan in view plane.
            Vector3 fwd = Vector3.Normalize(Target - Eye());
            Vector3 right = Vector3.Normalize(Vector3.Cross(fwd, Vector3.UnitY));
            Vector3 up = Vector3.Cross(right, fwd);
            float k = PanSpeed * Distance;
            Target += (-right * delta.X + up * delta.Y) * k;
        }

        float wheel = Raylib.GetMouseWheelMove();
        if (wheel != 0.0f)
            Distance = Math.Clamp(Distance * (1.0f - wheel * ZoomSpeed), 0.05f, 5000.0f);

        if (Raylib.IsKeyPressed(KeyboardKey.F))
            Frame(Vector3.Zero, 8.0f);
    }

    public void Frame(Vector3 center, float radius)
    {
        Target = center;
        Distance = MathF.Max(radius * 2.0f, 0.5f);
    }
}
