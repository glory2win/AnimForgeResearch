# 01 — Architecture

## The shape of the tool

Prani runs as **one process with two windows**:

```
┌─────────────────────────────┐        ┌──────────────────────────────────────┐
│  Prani Control Center       │        │  Prani — Workspace                   │
│  (Avalonia, main thread)    │        │  (raylib window, render thread)      │
│                             │        │  ┌─────────────┬─────────────┐       │
│  File ▸ New/Open/Import/    │ cmds   │  │ Perspective │    Top      │  ImGui│
│         Save/Save As/Exit   │ ─────► │  ├─────────────┼─────────────┤  dock-│
│  Outliner (Avalonia twin)   │        │  │   Front     │   Right     │  space│
│  Properties (NumericUpDown) │ ◄───── │  ├─────────────┴─────────────┤       │
│  Log view                   │ snap-  │  │ Heatmap│Stats│Console│... │       │
└─────────────────────────────┘ shots  │  └───────────────────────────┘       │
                                        └──────────────────────────────────────┘
```

Why two windows instead of embedding raylib inside Avalonia:

* raylib owns exactly **one OS window + GL context** and its API is thread-affine.
  Embedding it as an Avalonia child control means either CPU-copying every frame into a
  `WriteableBitmap` (slow, kills the point of raylib) or fragile Win32 `SetParent` hacks.
* A separate workspace window is what DCCs do anyway (Maya's viewport vs. its Qt panels
  are separate render paths), and it keeps each UI technology on its home turf.
* ImGui inside the raylib window gives us **dockable multi-viewport** layouts for free.

## Layer rules (enforced by project references)

```
AnimForge.Core   ← pure math. References: nothing.
      ▲
Prani.Engine     ← raylib + ImGui + Assimp. References: Core. NEVER Avalonia.
      ▲
Prani.App        ← Avalonia. References: Engine (only RenderHost/commands/snapshots/Log).
```

`Prani.App` never calls raylib or ImGui. `Prani.Engine` never sees a button or a dialog.
Everything crossing the boundary is a plain record: `IEngineCommand` going in,
`SceneSnapshot` / `LogEntry` coming out. This is what makes the shell swappable — the
same engine could be driven by a CLI, a test harness, or a different UI toolkit.

## Data flow for a typical action

"Import FBX" from the menu:

1. `MainWindow` (Avalonia) shows the file picker → gets a path.
2. `MainViewModel.ImportModel(path)` → `EngineService` → `RenderHost.Enqueue(new ImportModelCommand(path))`.
3. Next frame, the render thread drains the queue: `FbxImporter.Import` runs **on the render
   thread** (GPU upload needs the GL context), a `SceneNode` is added, perspective camera frames it.
4. `RenderHost` publishes a `SceneSnapshot`; `EngineService` marshals it to the UI thread;
   `MainViewModel` rebuilds the outliner list and the title bar dirty marker.
5. The ImGui panels don't need any of that — they read the live scene directly next frame
   (same thread), but their **writes** still go through the same command queue.

## The pieces

| File | Responsibility |
|---|---|
| `Prani.Engine/RenderHost.cs` | Window lifetime, frame loop, command dispatch — the composition root of the engine |
| `Prani.Engine/Scene/Scene3D.cs` | The document (nodes, selection, dirty, file path) |
| `Prani.Engine/Viewport/Viewport3D.cs` | One render-texture view inside an ImGui window |
| `Prani.Engine/UI/ImGuiLayer.cs` | rlImGui setup + dockspace + panel iteration |
| `Prani.App/Services/EngineService.cs` | Thread marshaling: the only place `Dispatcher.UIThread` appears |
| `Prani.App/MainWindow.axaml(.cs)` | File menu, pickers, confirm dialogs |
| `AnimForge.Core/All.cs` | `animforge_core.all` facade |

## Frame anatomy (render thread)

```
while (!stop && !WindowShouldClose)
    DrainCommands()                       // mutate scene; publish snapshot if changed
    foreach viewport: RenderContent()     // 3D scene → RenderTexture
    BeginDrawing
        ImGui frame:
            DockSpaceOverViewport
            main menu bar (panel toggles)
            viewport windows (blit their textures, route camera input)
            panels (outliner / properties / heatmap / stats / console)
    EndDrawing
```

Scene state is mutated in exactly one place (`DrainCommands` → `Apply`) — everything else
is read-only rendering. Keep it that way; it is the invariant the whole tool rests on.
