# JacobianDLSIK — Integration Guide

How to get the module compiling, wired into an AnimBP, tuned, and profiled.
Targets UE 5.3–5.6.

## 1. File placement & Build.cs

| file | module |
|---|---|
| `JacobianDLSSolver.h/.cpp` | **runtime** game module |
| `AnimNode_JacobianDLSIK.h/.cpp` | **runtime** game module |
| `AnimGraphNode_JacobianDLSIK.h/.cpp` | **editor** (or UncookedOnly) module |
| `Tests/JacobianDLSSolverTests.cpp` | runtime module (compiles out in shipping via `WITH_DEV_AUTOMATION_TESTS`) |

Replace `YOURGAME_API` with your module's API macro (or delete it for a
single-module project).

Runtime module `Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "AnimGraphRuntime"
});
```

Editor module `Build.cs` (for the graph node):

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine",
    "AnimGraph", "AnimGraphRuntime", "BlueprintGraph", "UnrealEd",
    "YourRuntimeModuleName"
});
```

No editor module yet? Create one via the `.uproject` Modules list with
`"Type": "Editor"`, or temporarily test the runtime node through a linked anim
layer — but the graph-node pair is the intended workflow.

Engine-version call sites to check if you're off 5.3–5.6:
`FAnimationRuntime::ConvertBoneSpaceTransformToCS` signature, and
`EAutomationTestFlags` composition in the tests file.

## 2. AnimBP wiring

1. In the AnimGraph, make sure you're in **component space** (the node is a
   skeletal control; UE inserts the space conversions automatically when you
   place it).
2. Add **Jacobian DLS IK** (category *AnimForge|IK*).
3. Set **RootBone** (e.g. `upperarm_l`) and **TipBone** (e.g. `hand_l`). Tip
   must be a descendant of root; every bone on the parent path between them
   becomes part of the solve.
4. Drive **EffectorLocation** from a pin (world-space hit location, prop socket,
   etc.) and set **EffectorLocationSpace** accordingly (`BCS_WorldSpace` for
   trace hits is typical).
5. Blend with the node's built-in **Alpha** pin — standard skeletal-control
   alpha semantics.

The tip bone's *rotation* is intentionally untouched (THEORY.md §3.2). For a
specific hand orientation, follow the node with a Transform (Modify) Bone or a
Look At on the tip.

### Per-joint tuning (JointSettings array)

One entry per bone you want to deviate from `Weight = 1, no limits`:

- Look-at spine chain: `spine_01` weight 0.15, `spine_02` 0.25, `neck_01` 0.6,
  `head` 1.0 → gaze distributes naturally, mostly in the neck and head.
- Arm reach: elbow entry with `bUseLimits`, swing ~130°, twist ~15°,
  TwistAxis X (mannequin) → no hyperextension, no forearm roll.

## 3. Tuning cheat sheet

| symptom | knob | direction |
|---|---|---|
| jitter near full extension | `Damping` / `MaxExtraDamping` | raise |
| sluggish, lags moving targets | `Damping` | lower; raise `MaxIterations` |
| doesn't quite reach easy targets | `Damping` too high, or `MaxIterations` too low | lower / raise |
| pops when leg approaches straight | `IsotropyThreshold` | raise (damping engages earlier) |
| chain "whips" on target teleports | `MaxErrorStep` | lower (≈ half chain length) |
| wrong joints doing the work | `JointSettings` weights | shift weight toward the joints you want |

Defaults are sized for a ~60 cm humanoid arm in cm units. For a tail or crane,
scale `Damping` and `MaxErrorStep` proportionally to chain length (THEORY.md §3.6
"Units and tuning").

## 4. Debugging

- **`a.AnimNode.JacobianDLSIK.Debug 1`** — draws the solved chain (yellow),
  target (green), effector (red) in PIE/preview.
- **AnimBP debug view** (Show Debug Anim / the AnimGraph debugger) — the node
  reports `iters / err / lambda / iso` per frame via `GatherDebugData`:
  - `iters` pinned at max + `err` large → unreachable target or budget too low.
  - `iso` near 0 → you are solving at a singularity; that's where `lambda`
    should be seen rising (adaptive damping working as intended).
- Automation tests (below) are the first thing to run after any solver edit.

## 5. Running the tests

Editor: **Tools ▸ Session Frontend ▸ Automation**, filter `AnimForge.JacobianDLSIK`.

Headless / CI:

```
UnrealEditor-Cmd.exe YourProject.uproject -ExecCmds="Automation RunTests AnimForge.JacobianDLSIK; Quit" -unattended -nopause -nullrhi -log
```

What each test proves is documented in [TESTING.md](TESTING.md), alongside the
manual in-editor scenarios (singularity torture, engine-node comparison, perf
methodology).
