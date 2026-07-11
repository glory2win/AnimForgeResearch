using ImGuiNET;
using Prani.Engine.Scene;

namespace Prani.Engine.UI.Panels;

/// <summary>Scene node list with selection, visibility toggles, and delete.</summary>
public sealed class OutlinerPanel : Panel
{
    public override string Title => "Outliner";

    public override void Draw(EngineContext ctx)
    {
        if (!Open) return;
        if (ImGui.Begin(Title, ref Open))
        {
            if (ctx.Scene.Nodes.Count == 0)
                ImGui.TextDisabled("Empty scene.\nFile > Import to add a mesh.");

            int? deleteId = null;
            foreach (var node in ctx.Scene.Nodes)
            {
                ImGui.PushID(node.Id);

                bool visible = node.Visible;
                if (ImGui.Checkbox("##vis", ref visible))
                    ctx.Enqueue(new SetNodeVisibleCommand(node.Id, visible));

                ImGui.SameLine();
                bool selected = node.Id == ctx.Scene.SelectedId;
                if (ImGui.Selectable(node.Name, selected))
                    ctx.Enqueue(new SelectNodeCommand(node.Id));

                if (ImGui.BeginPopupContextItem("node_ctx"))
                {
                    if (ImGui.MenuItem("Delete"))
                        deleteId = node.Id;
                    ImGui.EndPopup();
                }
                ImGui.PopID();
            }

            if (deleteId is int id)
                ctx.Enqueue(new RemoveNodeCommand(id));
        }
        ImGui.End();
    }
}
