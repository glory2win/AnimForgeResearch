using System.Numerics;
using ImGuiNET;

namespace Prani.Engine.UI.Panels;

/// <summary>Log viewer (ImGui side; the Avalonia shell shows the same Log sink).</summary>
public sealed class ConsolePanel : Panel
{
    public override string Title => "Console";

    private bool _autoScroll = true;

    public override void Draw(EngineContext ctx)
    {
        if (!Open) return;
        if (ImGui.Begin(Title, ref Open))
        {
            if (ImGui.Button("Clear")) Log.Clear();
            ImGui.SameLine();
            ImGui.Checkbox("Auto-scroll", ref _autoScroll);
            ImGui.Separator();

            if (ImGui.BeginChild("log_scroll"))
            {
                foreach (var entry in Log.Snapshot())
                {
                    Vector4 color = entry.Level switch
                    {
                        LogLevel.Warning => new Vector4(1.0f, 0.8f, 0.3f, 1.0f),
                        LogLevel.Error => new Vector4(1.0f, 0.4f, 0.4f, 1.0f),
                        _ => new Vector4(0.8f, 0.8f, 0.8f, 1.0f),
                    };
                    ImGui.TextColored(color, $"[{entry.Time:HH:mm:ss}] {entry.Message}");
                }
                if (_autoScroll && ImGui.GetScrollY() >= ImGui.GetScrollMaxY() - 4)
                    ImGui.SetScrollHereY(1.0f);
            }
            ImGui.EndChild();
        }
        ImGui.End();
    }
}
