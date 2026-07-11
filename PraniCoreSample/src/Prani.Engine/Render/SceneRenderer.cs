using System.Numerics;
using Raylib_cs;
using Prani.Engine.Scene;
using Prani.Engine.Viewport;

namespace Prani.Engine.Render;

/// <summary>
/// Draws grid, axes, scene nodes, and the selection highlight into the CURRENT 3D mode
/// (call inside BeginMode3D/EndMode3D — Viewport3D.RenderContent does that).
/// </summary>
public sealed class SceneRenderer
{
    public bool DrawGrid = true;
    public bool BackfaceCulling = true;
    public int DrawCallsLastFrame { get; private set; }

    public void Draw(Scene3D scene, Camera3D camera, Viewport3D viewport)
    {
        DrawCallsLastFrame = 0;

        if (!BackfaceCulling) Rlgl.DisableBackfaceCulling();

        if (DrawGrid)
        {
            Raylib.DrawGrid(24, 1.0f);
            // World axes: X red, Y green, Z blue (1 unit).
            Raylib.DrawLine3D(Vector3.Zero, Vector3.UnitX, Color.Red);
            Raylib.DrawLine3D(Vector3.Zero, Vector3.UnitY, Color.Green);
            Raylib.DrawLine3D(Vector3.Zero, Vector3.UnitZ, Color.Blue);
        }

        foreach (var node in scene.Nodes)
        {
            if (!node.Visible || node.Model is null) continue;

            // System.Numerics matrices are row-major; raylib expects column-major.
            Matrix4x4 world = Matrix4x4.Transpose(node.WorldMatrix());

            foreach (var slot in node.Model.Slots)
            {
                Raylib.DrawMesh(slot.Mesh, slot.Material, world);
                DrawCallsLastFrame++;
            }

            if (node.Id == scene.SelectedId)
                DrawSelection(node);
        }

        if (!BackfaceCulling) Rlgl.EnableBackfaceCulling();
    }

    private static void DrawSelection(SceneNode node)
    {
        if (node.Model is null) return;
        // Transform the local AABB corners and wrap a world AABB around them (fast, conservative).
        var b = node.Model.Bounds;
        Matrix4x4 m = node.WorldMatrix();
        Vector3 min = new(float.MaxValue), max = new(float.MinValue);
        for (int i = 0; i < 8; i++)
        {
            var corner = new Vector3(
                (i & 1) == 0 ? b.Min.X : b.Max.X,
                (i & 2) == 0 ? b.Min.Y : b.Max.Y,
                (i & 4) == 0 ? b.Min.Z : b.Max.Z);
            var w = Vector3.Transform(corner, m);
            min = Vector3.Min(min, w);
            max = Vector3.Max(max, w);
        }
        Raylib.DrawBoundingBox(new BoundingBox(min, max), Color.Orange);
    }
}
