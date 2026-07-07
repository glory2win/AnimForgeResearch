# AnimForgeLiveLink — Bidirectional Motion Warping Bridge (Maya ⇄ Unreal)

A production tool that removes the *export → import → set up → test → repeat* loop
when authoring motion-warped animations. The animator keeps working in Maya; the
Unreal **gym** evaluates the warp exactly as the engine will at runtime and streams
the resulting root trajectory and ghost poses straight back into the Maya scene.

```
        ┌────────────────────── Maya ──────────────────────┐
        │  PySide UI (animforge_warpviz_ui.py)             │
        │    time range · warp method · warp target loc    │
        │            │ Evaluate                            │
        │            ▼                                     │
        │  AnimForgeMayaWarpViz.mll (animForgeWarpViz cmd) │
        │    samples root joint + target locator           │
        └────────────┬───────────────────────▲─────────────┘
                     │ EvaluateRequest       │ EvaluateResult
                     │ (TCP, framed JSON)    │ (trajectory + ghosts)
        ┌────────────▼───────────────────────┴─────────────┐
        │  AnimForgeUnrealWarpViz plugin (gym project)     │
        │    clip registry → root motion extraction        │
        │    → SkewWarp/SimpleWarp/Scale → ghost sampling  │
        └───────────────────────────────────────────────────┘
```

## The animator loop

1. Author the animation in Maya, open the WarpViz UI
   (`import animforge_warpviz_ui; animforge_warpviz_ui.show()`).
2. Set the time range (defaults to playback range), pick a
   **RootMotionWarpingMethod** (`SkewWarp`, `SimpleWarp`, `Scale`) and a **Warp
   Target** locator, press **Evaluate**.
3. The `.mll` samples the scene, connects to the gym and ships an
   `EvaluateRequest`.
4. The gym resolves the clip from its registry, extracts root motion, applies
   the warp (the same math the MotionWarping modifier applies in game), samples
   ghost poses, and answers.
5. Maya receives the result on an idle callback and builds, on the
   `AnimForgeWarpViz_Result` anim layer:
   * a green NURBS curve through the **warped** root trajectory,
   * a templated gray curve for the **original** trajectory (side-by-side),
   * ghost locators (`AnimForgeWarpViz_Ghosts`) keyed at their source frames.
6. The animator nudges the warp target locator and evaluates again — seconds,
   not minutes.

## Repository layout

```
AnimForgeLiveLink/
├── README.md / THEORY.md / TESTING.md
├── UnrealGymProject/
│   ├── AnimForgeGym.uproject            # gym project (L_WarpGym level + character)
│   ├── Shared/AnimForgeWarpVizShared/   # ★ one source of truth, compiled into both plugins
│   │   ├── WarpVizTypes.h               #   Vec3/Quat/Mat3 + domain types
│   │   ├── WarpVizJson.h/.cpp           #   dependency-free JSON (Maya has none, UE's is too heavy)
│   │   ├── WarpVizProtocol.h/.cpp       #   framing + typed messages
│   │   └── SkewWarpMath.h               #   the warp itself (see THEORY.md)
│   ├── Source/
│   │   ├── AnimForgeGym/                # minimal game module (gym is content)
│   │   └── Programs/AnimForgeMayaWarpViz/   # ★ the Maya .mll (UBT Program target)
│   │       ├── AnimForgeMayaWarpViz.Build.cs / .Target.cs / BuildMayaPlugin.bat
│   │       ├── Private/
│   │       │   ├── MayaPluginMain.cpp   #   plugin entry + idle drain
│   │       │   ├── WarpVizCommand.*     #   animForgeWarpViz MPxCommand
│   │       │   ├── WarpVizClient.*      #   Winsock client, recv thread
│   │       │   ├── MayaSceneExtractor.* #   root sampling + target transform
│   │       │   └── ResultImporter.*     #   anim layer / curves / ghosts
│   │       └── Scripts/
│   │           ├── animforge_warpviz_ui.py    # PySide2/6 dialog
│   │           ├── warpviz_session.py         # UI-free Evaluate logic (unit-tested)
│   │           ├── warpviz_protocol.py        # 1:1 Python protocol mirror
│   │           ├── warpviz_skewwarp.py        # 1:1 Python math mirror
│   │           └── mock_unreal_server.py      # gym stand-in, no engine needed
│   └── Plugins/AnimForgeUnrealWarpViz/  # ★ the gym-side UE plugin
│       └── Source/AnimForgeUnrealWarpViz/
│           ├── Public/  WarpVizServer.h · WarpVizEvaluator.h · WarpVizSettings.h
│           └── Private/ …impl + Tests/ (UE Automation, "AnimForge.WarpViz.*")
└── Tests/
    ├── Python/   # runnable now: python Tests/Python/run_all.py   (56 tests)
    └── Cpp/      # runnable now: Tests/Cpp/build_and_run.bat      (113 checks)
```

## Building

### Maya plugin (.mll)
```bat
set UE_ROOT=C:\Epic\UE_5.4
set MAYA_SDK=C:\devkits\Maya2025\devkitBase
UnrealGymProject\Source\Programs\AnimForgeMayaWarpViz\BuildMayaPlugin.bat
```
The bat runs UBT (Program target, no engine modules linked → nothing engine-ish
leaks into Maya), then copies the DLL to `AnimForgeMayaWarpViz.mll`. Add
`UnrealGymProject\Binaries\Win64` to `MAYA_PLUG_IN_PATH` and the `Scripts/`
folder to `PYTHONPATH`.

### Gym project
Generate project files for `AnimForgeGym.uproject` and build the editor target;
the `AnimForgeUnrealWarpViz` plugin builds with it. Then in
**Project Settings → Plugins → AnimForge WarpViz**:
* set the listen port (default `46464`, matches the Maya default),
* register clips: `ClipName` (what animators type in Maya) → the imported
  `UAnimSequence`/`UAnimMontage`, plus optional `GhostBones`,
* optionally restrict `AcceptedCharacterIds`.

The server auto-starts with the editor (`bAutoStartServer`). Open `L_WarpGym`.

## Trying the loop without Unreal

`mock_unreal_server.py` speaks the full protocol and evaluates warps with the
Python mirror of the shared math:

```bat
python UnrealGymProject\Source\Programs\AnimForgeMayaWarpViz\Scripts\mock_unreal_server.py
```

Point the Maya UI at `127.0.0.1:46464` and the whole Maya-side pipeline —
UI → .mll → socket → result import — runs against the mock. Great for animator
onboarding and for developing the Maya side on machines without the engine.

## Design decisions worth knowing

* **Custom framed-JSON protocol instead of Unreal LiveLink transport.** LiveLink
  is built for continuous subject streaming; this tool is request/response with
  bulky one-shot payloads. A 12-byte framed JSON protocol keeps the Maya plugin
  free of every engine dependency and made a Python mock server trivial.
* **The gym evaluates root motion directly (no PIE round trip).** Root-motion
  extraction plus the modifier math is exactly what the MotionWarping component
  does at runtime, minus gameplay noise — and it is deterministic and fast. The
  evaluator is structured so a PIE-capture strategy can be added later
  (`FWarpVizEvaluator::Evaluate` is the single seam).
* **Maya ships its own root samples with the request.** The gym compares them
  against the extracted asset root motion and warns when drift exceeds a
  threshold — catching the classic "the gym has a stale import" failure before
  it wastes an iteration.
* **All Maya scene edits happen on the idle callback**, never the socket
  thread; results land on a dedicated anim layer so animator keys are never
  touched.

See [THEORY.md](THEORY.md) for the warp math derivation and
[TESTING.md](TESTING.md) for the full test matrix.
