# JacobianDLSIK — Test Plan

Two layers: automated math tests (headless, CI-able) and manual in-editor
scenarios (visual quality + comparison against engine solvers). Every automated
test maps to a specific claim in THEORY.md — that's deliberate: the test suite
*is* the proof that the theory doc isn't fiction.

## 1. Automated tests ([Tests/JacobianDLSSolverTests.cpp](Tests/JacobianDLSSolverTests.cpp))

Run: Session Frontend ▸ Automation ▸ filter `AnimForge.JacobianDLSIK`, or
headless via `-ExecCmds="Automation RunTests AnimForge.JacobianDLSIK; Quit"`.

| test | claim it proves | THEORY.md |
|---|---|---|
| `FiniteDifferenceJacobian` | the analytic column $a\times(p_e-p_i)$ matches a finite-difference probe of FK — the solver descends the true gradient | §3 |
| `ReachableTargetConverges` | in-workspace targets are hit within tolerance in the iteration budget | §2 |
| `DampingBoundsSingularStep` | **the core DLS claim**: near-singular pose, on-axis target → undamped step explodes (>1 rad), damped step stays bounded (<0.35 rad), with all safety clamps disabled so λ alone does the bounding | §5–6 |
| `UnreachableTargetStable` | out-of-reach targets: chain extends, points at the target (alignment >0.995), settles at the workspace boundary, no NaN/oscillation | §10 |
| `JointLimitsRespected` | swing/twist limits are hard constraints under a target demanding far more bend | §10 |
| `LockedJointDoesNotMove` | weight 0 locks a joint bit-exactly | §9 |
| `Deterministic` | identical inputs → identical outputs (worker threads, replay, netsync) | — |
| `LongChainPerformance` | 30-joint × 12-iteration worst case; logs µs/solve, loose CI assert | §7, DESIGN §5 |

Notes on test design:

- Tests hit `FJacobianDLSSolver` directly — no skeleton assets, no world — so
  they run in seconds and never flake on content.
- `DampingBoundsSingularStep` uses a *near*-singular pose (2° bend), not an
  exact one: exactly-singular is the benign case (the response annihilates);
  σ small-but-nonzero is where 1/σ actually detonates. See THEORY.md §5
  "Why near singular is worse".
- The perf test forces all iterations via an unreachable target + tiny
  tolerance so it measures worst case, not a lucky early-out.

## 2. Manual scenario: singularity torture (the money shot)

The test that shows why this node exists. Mannequin, arm chain
(`upperarm_l → hand_l`), effector on a slow-moving debug sphere:

1. Move the target from near the chest **outward through full arm extension and
   10 cm beyond**, then back, repeatedly.
2. Same setup with `AnimNode_CCDIK` and `AnimNode_Fabrik` on the same chain.
3. Watch for: elbow jitter as the arm approaches straight, elbow pop/flip when
   crossing full extension, roll drift in the forearm (FABRIK's weakness).

Expected: DLS eases into extension and eases out — no frame where the elbow
snaps. With `a.AnimNode.JacobianDLSIK.Debug 1` and the AnimBP debugger you
should see `iso` collapse toward 0 and `lambda` rise exactly at full extension.
That's THEORY.md §8 visible on screen.

## 3. Manual scenario: foot planting on a slope

Leg chain (`thigh_l → foot_l`), effector from a downward trace, walk on a ramp:

- Foot reaches the traced point within `Precision` on slopes within leg reach.
- On slopes that force near-full leg extension, the knee must not vibrate
  (compare Two Bone IK, which is exact but snaps at full extension).
- Add a knee `JointSettings` entry with limits; verify the knee never bends
  backwards regardless of trace point.

## 4. Manual scenario: performance comparison

Methodology for an honest engine-node comparison (numbers vary per machine —
publish the method, not just the numbers):

1. One map, 100 instances of the same character AnimBP, camera fixed.
2. Variant A: this node (4-joint arm, `MaxIterations 8`). Variant B: CCD, same
   chain, iteration count matched. Variant C: FABRIK likewise.
3. Measure with Unreal Insights (`-trace=cpu`), filter to
   `EvaluateSkeletalControl`; or `stat anim` averaged over 30 s.
4. Also record the *quality* axis: iterations actually needed to reach 0.1 cm on
   a moving target (from `GatherDebugData`) — DLS typically converges in fewer
   iterations than CCD near difficult poses, so equal-iteration comparisons
   undersell it.

## 5. Regression discipline

- Any solver change: run the automation suite first (`FiniteDifferenceJacobian`
  catches 90% of math typos instantly).
- Any default-tuning change: re-run scenario §2 — stability near singularities
  is the product; do not trade it away for convergence-speed wins in easy poses.
- Keep the perf test's logged µs/solve in the commit message when touching the
  inner loop.
