using ImGuiNET;
using Raylib_cs;

namespace Prani.Engine.UI.Panels;

/// <summary>Frame timing + scene statistics + a couple of render toggles.</summary>
public sealed class StatsPanel : Panel
{
    public override string Title => "Stats";

    public override void Draw(EngineContext ctx)
    {
        if (!Open) return;
        if (ImGui.Begin(Title, ref Open))
        {
            ImGui.Text($"FPS: {Raylib.GetFPS()}   frame: {ctx.FrameTimeMs:F2} ms");
            ImGui.Text($"Nodes: {ctx.Scene.Nodes.Count}");

            int meshes = 0, tris = 0;
            foreach (var n in ctx.Scene.Nodes)
            {
                meshes += n.Model?.Slots.Count ?? 0;
                tris += n.Model?.TriangleCount ?? 0;
            }
            ImGui.Text($"Meshes: {meshes}   Triangles: {tris:N0}");
            ImGui.Text($"Draw calls (3D): {ctx.Renderer.DrawCallsLastFrame}");

            ImGui.Separator();
            ImGui.Checkbox("Grid", ref ctx.Renderer.DrawGrid);
            ImGui.Checkbox("Backface culling", ref ctx.Renderer.BackfaceCulling);

            ImGui.Separator();
            ImGui.TextDisabled($"animforge_core {AnimForge.Core.All.Version}");
        }
        ImGui.End();
    }
}
