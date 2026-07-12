using System.Numerics;
using AnimForge.Core.Maths;
using AnimForge.Core.Viz;
using ImGuiNET;

namespace Prani.Engine.UI.Panels;

/// <summary>
/// Heatmap observation panel — the template for every future "field of scalars" view
/// (foot-slide error, joint speed, pose distance…). The demo field is spring overshoot
/// from animforge_core: x-axis = frequency, y-axis = halflife, color = max overshoot.
/// Rendering is pure ImDrawList quads; swap ComputeField() to visualize anything else.
/// </summary>
public sealed class HeatmapPanel : Panel
{
    public override string Title => "Heatmap";

    private readonly HeatmapGrid _grid = new(64, 48);
    private float _freqMin = 0.1f, _freqMax = 6.0f;
    private float _halflifeMin = 0.02f, _halflifeMax = 1.0f;
    private int _colormap; // 0 viridis, 1 coolwarm
    private bool _dirty = true;

    public override void Draw(EngineContext ctx)
    {
        if (!Open) return;
        if (ImGui.Begin(Title, ref Open))
        {
            ImGui.TextDisabled("Spring overshoot vs (frequency, halflife) — animforge_core.SpringExact");

            _dirty |= ImGui.SliderFloat("Freq max (Hz)", ref _freqMax, 1.0f, 12.0f);
            _dirty |= ImGui.SliderFloat("Halflife max (s)", ref _halflifeMax, 0.1f, 2.0f);
            _dirty |= ImGui.Combo("Colormap", ref _colormap, "Viridis\0Coolwarm\0");

            if (_dirty)
            {
                ComputeField();
                _dirty = false;
            }

            DrawGrid();
        }
        ImGui.End();
    }

    private void ComputeField()
    {
        _grid.Fill((u, v) =>
        {
            float freq = _freqMin + u * (_freqMax - _freqMin);
            float halflife = _halflifeMin + v * (_halflifeMax - _halflifeMin);
            return SpringDamper.MeasureOvershoot(freq, halflife, simSeconds: 2.0f, dt: 1.0f / 60.0f);
        });
    }

    private void DrawGrid()
    {
        Vector2 avail = ImGui.GetContentRegionAvail();
        avail.Y -= 4;
        // A docked panel can be squeezed to zero/negative content size while dragging
        // splitters — InvisibleButton with a non-positive size triggers an ImGui assert,
        // which aborts the whole process. Bail out instead of crashing.
        if (avail.X < 16.0f || avail.Y < 16.0f)
            return;
        Vector2 origin = ImGui.GetCursorScreenPos();
        var drawList = ImGui.GetWindowDrawList();

        float cellW = avail.X / _grid.Width;
        float cellH = avail.Y / _grid.Height;

        for (int y = 0; y < _grid.Height; y++)
        {
            for (int x = 0; x < _grid.Width; x++)
            {
                float t = _grid.Normalized(x, y);
                Vector3 rgb = _colormap == 0 ? Colormap.Viridis(t) : Colormap.Coolwarm(t);
                var p0 = new Vector2(origin.X + x * cellW, origin.Y + (_grid.Height - 1 - y) * cellH);
                var p1 = new Vector2(p0.X + cellW + 0.5f, p0.Y + cellH + 0.5f);
                drawList.AddRectFilled(p0, p1, Colormap.ToRgba(rgb));
            }
        }

        // Invisible button claims the area so hover/tooltip works and the window doesn't drag.
        ImGui.InvisibleButton("heatmap_area", avail);
        if (ImGui.IsItemHovered())
        {
            Vector2 mouse = ImGui.GetMousePos() - origin;
            int cx = Math.Clamp((int)(mouse.X / cellW), 0, _grid.Width - 1);
            int cy = Math.Clamp(_grid.Height - 1 - (int)(mouse.Y / cellH), 0, _grid.Height - 1);
            float freq = _freqMin + (cx / (float)(_grid.Width - 1)) * (_freqMax - _freqMin);
            float halflife = _halflifeMin + (cy / (float)(_grid.Height - 1)) * (_halflifeMax - _halflifeMin);
            ImGui.SetTooltip($"freq {freq:F2} Hz\nhalflife {halflife:F3} s\novershoot {_grid[cx, cy]:F3}");
        }
    }
}
