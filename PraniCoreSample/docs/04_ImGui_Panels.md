# 04 — ImGui panels

## Integration

`rlImgui-cs` bridges raylib ⇄ Dear ImGui: it owns the font atlas texture, translates
raylib input into ImGui IO, and renders draw lists through rlgl. Setup is two lines
(`ImGuiLayer.Setup`):

```csharp
rlImGui.Setup(darkTheme: true, enableDocking: true);
io.ConfigWindowsMoveFromTitleBarOnly = true;   // so viewport drags orbit instead of moving windows
```

Each frame: `rlImGui.Begin()` → build UI → `rlImGui.End()`. The dockspace is one call:

```csharp
ImGui.DockSpaceOverViewport(0, ImGui.GetMainViewport(), ImGuiDockNodeFlags.PassthruCentralNode);
```

Users drag tabs to arrange viewports/panels; ImGui persists the layout in `imgui.ini`.

## Panel contract

```csharp
public abstract class Panel
{
    public abstract string Title { get; }
    public bool Open = true;
    public abstract void Draw(EngineContext ctx);
}
```

* Panels run **on the render thread** → they may **read** `ctx.Scene` directly, zero
  locking, always-current data.
* All **writes** go through `ctx.Enqueue(command)` — same path the Avalonia shell uses,
  so the UI snapshot stays consistent and undo (future) has a single choke point.
* Register in `ImGuiLayer.Panels`; the "Panels" menu in the workspace menu bar
  auto-lists it with a visibility toggle.

Adding a panel is: subclass, implement `Draw`, add one line to the list.

## The Heatmap panel — the template for observation views

`HeatmapPanel` demonstrates the intended pattern for every future scalar-field view
(foot-slide error maps, joint speed over time × joints, pose-distance matrices):

1. **Data** lives in `AnimForge.Core.Viz.HeatmapGrid` — a W×H float field with min/max
   normalization. Filling is one lambda: `grid.Fill((u,v) => f(u,v))`.
2. **Color** comes from `AnimForge.Core.Viz.Colormap` — viridis (default) or coolwarm,
   returned as RGB and packed with `Colormap.ToRgba` for ImGui.
3. **Rendering** is raw `ImDrawList.AddRectFilled` per cell — no textures, no allocations,
   fast enough up to ~10⁴ cells. Above that, render into a raylib texture instead.
4. **Interaction**: an `InvisibleButton` over the grid area gives hover → tooltip with the
   exact cell value and its domain coordinates.

The demo field is real core math: max overshoot of `SpringDamper.SpringExact` as a
function of (frequency, halflife). Recomputed only when a slider changes (`_dirty` flag) —
64×48 cells × a 2 s sim each is ~1 M spring steps, fine on demand, wasteful per-frame.

To visualize your own data, replace `ComputeField()` and the axis labels in the tooltip.

## Existing panels

| Panel | Notes |
|---|---|
| `OutlinerPanel` | Selection + visibility + right-click delete; mirror of the Avalonia outliner |
| `PropertiesPanel` | `DragFloat3` transform editing |
| `HeatmapPanel` | See above |
| `StatsPanel` | FPS, tris, draw calls; grid & backface-culling toggles |
| `ConsolePanel` | Reads the shared `Log` ring buffer; auto-scroll |

## Style tips learned the hard way

* Push `ImGuiStyleVar.WindowPadding = (1,1)` for viewport windows or you get a white ring
  around the image.
* Gate camera input on `IsItemHovered()`/`IsWindowFocused()` or every viewport orbits at once.
* Use `ImGui.PushID(node.Id)` in list loops — duplicate labels otherwise merge widget state.
