using ImGuiNET;
using Prani.Engine.UI.Panels;
using rlImGui_cs;

namespace Prani.Engine.UI;

/// <summary>
/// Dear ImGui integration for the raylib window (via rlImGui-cs) plus the dockspace
/// that hosts viewports and panels. Layout persists automatically in imgui.ini.
/// </summary>
public sealed class ImGuiLayer : IDisposable
{
    public List<Panel> Panels { get; } = new()
    {
        new OutlinerPanel(),
        new PropertiesPanel(),
        new HeatmapPanel(),
        new StatsPanel(),
        new ConsolePanel(),
    };

    public void Setup()
    {
        rlImGui.Setup(darkTheme: true, enableDocking: true);
        var io = ImGui.GetIO();
        io.ConfigWindowsMoveFromTitleBarOnly = true; // don't steal viewport drags
    }

    /// <summary>One ImGui frame: dockspace + panel windows + viewport windows.</summary>
    public void DrawFrame(EngineContext ctx, Action drawViewports, Action<EngineContext> drawMainMenu)
    {
        rlImGui.Begin();

        ImGui.DockSpaceOverViewport(0, ImGui.GetMainViewport(), ImGuiDockNodeFlags.PassthruCentralNode);

        drawMainMenu(ctx);
        drawViewports();
        foreach (var panel in Panels)
            panel.Draw(ctx);

        rlImGui.End();
    }

    public void Dispose() => rlImGui.Shutdown();
}
