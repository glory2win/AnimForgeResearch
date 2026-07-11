using System.Numerics;
using ImGuiNET;
using Prani.Engine.Scene;

namespace Prani.Engine.UI.Panels;

/// <summary>Transform editor for the selected node (ImGui side; Avalonia has a twin view).</summary>
public sealed class PropertiesPanel : Panel
{
    public override string Title => "Properties";

    public override void Draw(EngineContext ctx)
    {
        if (!Open) return;
        if (ImGui.Begin(Title, ref Open))
        {
            var node = ctx.Scene.Selected;
            if (node is null)
            {
                ImGui.TextDisabled("Nothing selected.");
            }
            else
            {
                ImGui.Text(node.Name);
                if (!string.IsNullOrEmpty(node.SourcePath))
                    ImGui.TextDisabled(Path.GetFileName(node.SourcePath));
                ImGui.Separator();

                Vector3 pos = node.Position, rot = node.RotationDeg, scale = node.Scale;
                bool changed = false;
                changed |= ImGui.DragFloat3("Position", ref pos, 0.05f);
                changed |= ImGui.DragFloat3("Rotation", ref rot, 0.5f);
                changed |= ImGui.DragFloat3("Scale", ref scale, 0.01f);

                if (changed)
                    ctx.Enqueue(new SetNodeTransformCommand(node.Id, pos, rot, scale));

                if (node.Model is not null)
                {
                    ImGui.Separator();
                    ImGui.TextDisabled($"{node.Model.Slots.Count} mesh(es), {node.Model.TriangleCount:N0} tris");
                }
            }
        }
        ImGui.End();
    }
}
