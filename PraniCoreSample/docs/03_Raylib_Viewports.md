# 03 — Raylib viewports

## One GL context, many views

raylib gives us a single window/context, so multiple viewports are **offscreen render
textures**, one per view, composited by ImGui:

```
Viewport3D
├── RenderTexture2D  _rt        (recreated when its ImGui window resizes)
├── Camera3D          Camera    (raylib camera struct)
└── OrbitCamera       Orbit     (our controller: yaw/pitch/distance/target)
```

Per frame (see `RenderHost.RunLoop`):

1. `Viewport3D.RenderContent(drawScene)` — `BeginTextureMode(_rt)`, clear,
   `BeginMode3D(Camera)`, draw grid + scene, end both.
2. `Viewport3D.DrawImGuiWindow()` — an ImGui window whose content is
   `rlImGui.ImageRenderTextureFit(_rt)`. When hovered, the window forwards raylib's raw
   mouse state to `OrbitCamera.HandleInput()`.

Render-texture size follows `ImGui.GetContentRegionAvail()` with a one-frame lag —
`EnsureRenderTexture` reallocates only when the size actually changed.

## The four default views

| View | Projection | Camera behavior |
|---|---|---|
| Perspective | Perspective, FovY 50° | Full orbit/pan/zoom |
| Top | Orthographic | Orbit locked (pitch −89°), pan/zoom only |
| Front | Orthographic | Orbit locked (yaw 0°) |
| Right | Orthographic | Orbit locked (yaw 90°) |

For orthographic cameras raylib reuses `FovY` as the **vertical world size**; we map it
to `Orbit.Distance * 1.5` so the wheel still zooms naturally.

## OrbitCamera math

Position is spherical around `Target`:

```
eye = target − distance · ( cos(pitch)·sin(yaw),  sin(pitch),  cos(pitch)·cos(yaw) )
```

* **Orbit**: yaw/pitch ± mouse-delta · 0.35°/px, pitch clamped to ±89° (gimbal guard).
* **Pan**: move `Target` in the camera's right/up plane, scaled by `distance` so screen-space
  speed is constant at any zoom.
* **Zoom**: multiplicative (`distance *= 1 − wheel·0.12`) — feels uniform across scales;
  clamped to [0.05, 5000].
* **F**: refocus on origin (extend to selection — the engine already frames imports via
  `RenderHost.FramePerspectiveOnScene`).

## Matrix convention gotcha (read before adding rendering code)

`System.Numerics` matrices are **row-major with row-vector convention**; raylib expects
column-major. Compose transforms normally:

```csharp
world = CreateScale(S) * CreateFromYawPitchRoll(...) * CreateTranslation(T);  // S→R→T
Raylib.DrawMesh(mesh, material, Matrix4x4.Transpose(world));                  // transpose at the boundary!
```

The transpose lives in exactly one place (`SceneRenderer.Draw`). Keep it that way.

## Adding a fifth viewport

```csharp
// ViewportManager
Viewports.Add(new Viewport3D("Back", ViewportKind.Front)); // then customize Orbit.YawDeg = 180
```

Everything else (render, dock, input) picks it up automatically. For a camera-from-bone
or turntable view, subclass or extend `OrbitCamera` and drive `Viewport3D.Camera` directly.

## What to build next here

* **Picking**: `Raylib.GetScreenToWorldRay` needs manual adaptation for render-texture
  viewports — build the ray from the viewport-local mouse position and the view/projection
  of that viewport's camera, then `Raylib.GetRayCollisionBox` per node AABB.
* **Gizmos**: draw translate/rotate handles inside `RenderContent` after the scene pass;
  hit-test with the same ray.
