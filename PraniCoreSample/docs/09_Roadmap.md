# 09 — Roadmap

Ordered so each step ships something visible while building toward the real product:
an animation inspection/authoring tool with animforge_core math at the center.

## M1 — Interaction polish (small, high value)

* [ ] **Viewport picking**: ray from viewport-local mouse → AABB test per node → select.
      (Ray construction notes in [03_Raylib_Viewports.md](03_Raylib_Viewports.md).)
* [ ] **F frames selection** (currently origin) — bounds already computed at import.
* [ ] **Translate gizmo**: three axis handles drawn after the scene pass; drag = plane/axis
      constraint solve; emits `SetNodeTransformCommand` (rotation/scale gizmos after).
* [ ] Import options dialog: uniform scale + up-axis, baked at import (kills the FBX
      units papercut).
* [ ] Grid sizing/subdivision options; viewport labels + view-axis widget.

## M2 — Skeletal animation (the core milestone)

* [ ] Importer v2: drop `PreTransformVertices`; read bones (offsets, weights) and
      animation channels into **core-owned** pose structures (`AnimForge.Core` gets
      `Skeleton`, `Pose`, `AnimClip` — math types, no Assimp deps).
* [ ] Clip sampling + playback transport panel (play/pause/scrub/loop, speed).
* [ ] CPU skinning into dynamic meshes first (`UpdateMeshBuffer`), GPU skinning shader after.
* [ ] **Skeleton drawing**: joints/bones as lines+octahedrons in every viewport.
* [ ] Heatmap-on-mesh: per-vertex scalar (weights, speed) → vertex colors — the 3D twin
      of the Heatmap panel.

## M3 — Tooling depth

* [ ] Curve editor panel (ImGui) over `AnimForge.Core` curves; visualize spring/damper
      responses interactively (tuning UI for gameplay-anim parameters).
* [ ] Distance-matching lab: load a clip, plot its `DistanceCurve`, scrub by distance.
* [ ] Two-bone IK playground: drag target/pole in viewport, solver overlay live.
* [ ] Undo/redo: command queue is the choke point — wrap `Apply` with inverse-command
      recording.
* [ ] Async import (worker parse → GPU-upload command) + progress toast.

## M4 — Product hardening

* [ ] Dock.Avalonia in the shell (recipe in [05_Avalonia_Shell.md](05_Avalonia_Shell.md)).
* [ ] Settings persistence (recent files, camera prefs, theme) — JSON in `%APPDATA%/Prani`.
* [ ] Diffuse texture import + simple lit shader (raylib default is unlit-ish tint).
* [ ] `AnimForge.Core.Tests` + CI (`dotnet test` on push).
* [ ] Crash-safe scene autosave; `.prani` schema versioning (field already written).
* [ ] Native `animforge_core` (C++/SIMD) behind the same `All` facade via P/Invoke —
      benchmark first; managed may be enough for tooling.

## Open questions to settle before M2

1. Pose data layout: SoA quaternion+translation buffers (SIMD-friendly, matches the
   LiveLink protocol work) vs simple struct arrays (easier to debug)?
2. One scene node per imported file vs a node per skeleton root with mesh children?
3. Does Prani stay a viewer/inspector, or head toward authoring (keyframing)? Affects
   how much undo/serialization infrastructure M3 needs.
