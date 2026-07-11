# PraniCoreSample

First sample project for **Prani** — AnimForge's standalone animation tool. It proves out the
three-layer stack chosen in the tools-strategy discussion:

| Layer | Tech | Role |
|---|---|---|
| 3D workspace | **raylib** (Raylib-cs 7) | Viewports, mesh rendering, cameras, GL ownership |
| In-workspace UI | **Dear ImGui** (ImGui.NET + rlImGui-cs, docking) | Dockable panels: viewports, heatmap, outliner, stats, console |
| Application shell | **Avalonia 11** | File menu (New/Open/Import/Save/Save As/Exit), dialogs, precise property editing, log |
| Math core | **animforge_core** (`AnimForge.Core`) | Springs/dampers, two-bone IK, distance matching, colormaps, heatmap fields |

Two windows, one process: the Avalonia **Control Center** (main thread) and the raylib
**Workspace** (dedicated render thread) talk through a thread-safe command queue.

## Quick start

```powershell
dotnet run --project src\Prani.App
```

Requires .NET 8+ SDK, Windows/Linux/macOS with OpenGL 3.3. First build downloads all
native dependencies (raylib, cimgui, assimp) via NuGet — no manual installs.

Then: **File ▸ Import Mesh (FBX)…** and pick any `.fbx` / `.obj` / `.gltf` / `.glb` / `.dae`.
The mesh appears in all four viewports; the Perspective camera auto-frames it.

Viewport controls: **LMB** orbit · **MMB / Shift+LMB** pan · **wheel** zoom · **F** frame origin.

## Layout

```
PraniCoreSample/
├── src/
│   ├── AnimForge.Core/        # animforge_core — pure math, zero UI/engine deps
│   │   ├── All.cs             # the umbrella facade (animforge_core.all)
│   │   ├── Maths/             # SpringDamper, TwoBoneIk, DistanceMatching, Curves
│   │   └── Viz/               # Colormap (viridis/coolwarm), HeatmapGrid
│   ├── Prani.Engine/          # raylib + ImGui. Owns the render thread.
│   │   ├── RenderHost.cs      # window, frame loop, command dispatch  ← start reading here
│   │   ├── Scene/             # Scene3D, nodes, commands, snapshots, .prani serializer
│   │   ├── Import/            # Assimp → raylib mesh conversion (FBX etc.)
│   │   ├── Viewport/          # Viewport3D (render-texture views), OrbitCamera
│   │   ├── Render/            # SceneRenderer (grid, meshes, selection)
│   │   └── UI/                # ImGuiLayer + Panels (Outliner/Properties/Heatmap/Stats/Console)
│   └── Prani.App/             # Avalonia shell. File menu, dialogs, MVVM.
└── docs/                      # ← detailed docs, one per subsystem
```

## Docs

1. [Architecture](docs/01_Architecture.md) — layers, windows, data flow
2. [Build & Run](docs/02_Build_And_Run.md) — prerequisites, troubleshooting
3. [Raylib viewports](docs/03_Raylib_Viewports.md) — render textures, cameras, multi-view
4. [ImGui panels](docs/04_ImGui_Panels.md) — docking, adding panels, the heatmap
5. [Avalonia shell](docs/05_Avalonia_Shell.md) — menu/commands, dialogs, Dock.Avalonia upgrade path
6. [FBX import](docs/06_FBX_Import.md) — Assimp pipeline, index limits, units/axes
7. [animforge_core](docs/07_AnimForge_Core.md) — the math, with derivations
8. [Threading & commands](docs/08_Threading_And_Commands.md) — the concurrency contract
9. [Roadmap](docs/09_Roadmap.md) — picking, gizmos, skeletons, animation playback

## Known sample-level simplifications

* Textures are not imported yet (diffuse color only) — see roadmap.
* Skeletal data is baked flat (`PreTransformVertices`) — static preview only for now.
* Panel docking layout persists via `imgui.ini` next to the executable.
