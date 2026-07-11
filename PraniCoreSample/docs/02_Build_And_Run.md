# 02 — Build & Run

## Prerequisites

* **.NET 8 SDK** or newer (`dotnet --version`)
* A GPU/driver with **OpenGL 3.3** (raylib's default backend)
* No manual native installs: NuGet delivers raylib, cimgui and assimp binaries
  (`Raylib-cs` 7.0.1, `ImGui.NET` 1.91.6.1, `rlImgui-cs` 3.2.0, `AssimpNet` 4.1.0).

## Commands

```powershell
# from PraniCoreSample/
dotnet build                          # build everything
dotnet run --project src\Prani.App    # launch (both windows)
```

Release build: `dotnet build -c Release`; publish a self-contained exe:

```powershell
dotnet publish src\Prani.App -c Release -r win-x64 --self-contained
```

## First session walkthrough

1. Launch. Two windows open: **Control Center** (Avalonia) and **Workspace** (raylib).
2. In the Workspace, drag window tabs to arrange the four viewports + panels; the layout
   persists in `imgui.ini`.
3. **File ▸ Import Mesh (FBX)…** (Ctrl+I) — pick a mesh. It appears in all viewports,
   selected, camera framed.
4. Tweak **Position/Rotation/Scale** in either the Avalonia Properties column or the
   ImGui Properties panel — both drive the same command queue.
5. **File ▸ Save** (Ctrl+S) → writes a `.prani` JSON scene (paths + transforms, no geometry).
6. **File ▸ Open** re-imports every referenced asset.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `DllNotFoundException: raylib` | Platform without a bundled native — check `runtimes/` in output; raylib-cs ships win/linux/osx x64+arm64 |
| Workspace window opens black | GL 3.3 unsupported (VM/remote desktop) — try updating drivers; raylib logs to console |
| `AssimpNet` load failure | AssimpNet 4.1 ships 64-bit natives; ensure you're not building x86 |
| FBX imports rotated 90° / tiny | Units & axes — see [06_FBX_Import.md](06_FBX_Import.md); fix with node Rotation/Scale or exporter settings |
| Mesh looks inside-out | Flipped winding from the exporter — untick **Backface culling** in the Stats panel |
| App won't exit | The render thread joins with a 5 s timeout on shutdown; check the log for a stuck import |
| Panels gone / weird layout | Delete `imgui.ini` next to the exe to reset the dock layout |

## Where builds land

`src/Prani.App/bin/Debug/net8.0/` — `Prani.App.exe`, all managed DLLs, `runtimes/`
native folders, and (after first run) `imgui.ini`.
