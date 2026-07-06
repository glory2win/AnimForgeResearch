# Advanced Distance Matching — Design

Custom, plugin-free distance matching for predictive locomotion stops in UE C++.
This document is the *why and how it works*; see [INTEGRATION.md](INTEGRATION.md) for the *how to wire it up*.

---

## 1. The bug

Directional stops carry an authored root-motion slide. Vanilla path following drives the
character all the way to the goal and only *then* the locomotion system enters the stop —
so the slide plays out **past** the goal:

```
 normal ground                          platform with no edge rails
 ─────────────────────────────          ─────────────────────┐
   sprint ──────────► ◉ goal              sprint ──────────► ◉ goal
                      ├──slide──┤                            ├──sli│de──┤
                      ▼ stops here                           ▼     │  ✗ falls
                      (fine, just late)                      edge ─┘
```

The AI picked a legal nav point near the edge; the *animation* overshot it. The nav data
was never wrong — the trigger timing was.

## 2. The core idea

The slide distance of every stop animation is **knowable in advance** — it's baked into
its root motion. So instead of stopping *at* the goal, subtract the authored stop
distance from the remaining path distance and trigger the stop **early**:

```
   sprint ────────► ✱ trigger here ├──authored slide──┤ ◉ lands ON the goal
                    │                                    │
                    R = D (remaining == authored stop distance)
```

That is the whole fix in one sentence. Everything else in this system exists to make that
sentence robust: frame quantization, short paths, repaths, corners, gait mismatches, and
the AI-request lifecycle.

## 3. Distance matching primer

**Distance matching** decouples an animation's *time* from the wall clock and drives it by
a *distance* instead. It needs two functions per animation:

- `C(t)` — **distance curve**: how much travel distance remains at anim time `t`.
  Monotonically non-increasing, `C(0) = D` (total authored travel), `C(end) = 0`.
- `C⁻¹(d)` — **matching function**: the anim time at which exactly `d` remains.

Epic's `AnimationLocomotionLibrary` plugin implements this with hand-authored `Distance`
float curves and thread-safe functions that scrub a Sequence Evaluator
(`DistanceMatchToTarget`, `AdvanceTimeByDistanceMatching`), plus
`PredictGroundMovementStopLocation` for capsule-driven characters. Our implementation
(`AdvancedDistanceMatchingLibrary`) re-creates that API surface on stock engine modules
(`AnimGraphRuntime` only — no plugin) and extends it in two ways:

1. **Automatic curve extraction.** `C(t)` is built at load time by sampling the sequence's
   root motion track (`BuildDistanceCurveFromRootMotion`): accumulate the root's path,
   then `C(tᵢ) = |RootPos(end) − RootPos(tᵢ)|` in 2D, forced monotonic. No re-authoring of
   existing stop anims. Hand-authored curves are still supported, in both sign
   conventions.

2. **Root-motion awareness.** Classic distance matching assumes the *capsule* drives
   (movement component brakes, non-root-motion anim scrubbed to match). Our stops are the
   opposite: **root motion drives the capsule**. That inverts the data flow into a
   feedback loop:

   ```
   anim time ──► root motion delta ──► capsule moves ──► distance shrinks ──► anim time
   ```

   The loop is stable as long as time advancement is (a) monotonic and (b) play-rate
   clamped — which is exactly what `AdvanceTimeClamped` enforces. A useful property falls
   out of root motion here: the slide distance is *deterministic*. Trigger at the right
   moment and the landing point is exact by construction, no physics variance.

## 4. The algorithm, formalized

Symbols:

| symbol | meaning |
|---|---|
| `D(e)` | authored stop distance of entry `e` (= `C_e(0)`, extracted at load) |
| `R` | remaining along-path distance to the goal (recomputed every tick) |
| `S` | straight-line 2D distance to the goal |
| `v` | current ground speed |
| `L` | total path length, measured when a new path appears |
| `M` | gait commit margin (config) |
| `k` | trigger lookahead in frames (config, default 1) |

### 4.1 Plan phase — once per move (the short-path sub-case)

When a new path (or repath/goal change) is detected:

```
if ∃ gait g : L ≥ D_dir(g) + M   →  plan = DirectionalStop, gait = fastest such g
elif L ≥ D_quick                 →  plan = QuickStop, gait = slowest
else                             →  plan = IdleOrStep (no predictive trigger)
```

`RecommendedGaitName` is broadcast so the movement system can cap sprint on paths that
are too short to *ever* stop from a sprint. `M` must budget the acceleration distance to
actually reach gait `g` — tune it against your movement tuning. The `IdleOrStep` tier is
inherently ledge-safe because engine braking arrival never overshoots.

### 4.2 Monitor phase — every tick while moving

Recompute `R` from the live path: distance to the next path point plus untraveled
segments after it (all 2D — capsule-vs-navpoint Z offsets must not inflate it). Select
the candidate entry (direction from local-space velocity quadrant, then closest
`ReferenceSpeed`), and fire on:

```
R ≤ D(e) + v·Δt·k
```

**Why the lookahead term matters.** Testing `R ≤ D` alone means at the trigger frame
`R ∈ (D − vΔt, D]` — the character lands up to one frame of travel *past* the goal.
Off the ledge, at sprint speeds, that's 10–15 cm. With the `+ v·Δt·k` bias the trigger
frame satisfies `R ∈ (D, D + vΔt·k]`, so the raw landing error is **short, never long**:

```
ε_landing = R_trigger − D  ∈  [0, v·Δt·k]     (short of the goal — safe side)
```

Short-and-safe beats long-and-dead near an edge, and the closed loop (4.3) removes the
remaining ε anyway.

**Corners.** If `R − S >` tolerance, a corner sits between us and the goal; a stop slide
would cut across it — off-path and potentially off-navmesh (the exact geometry this
system protects). While corner-blocked, candidates must fit inside the current segment,
otherwise selection defers; after the corner, normal or late-trigger logic resumes.

### 4.3 Stop phase — closed-loop distance matching

Open loop (trigger only) already fixes the bug. The closed loop makes the landing exact
and absorbs every remaining error source (trigger quantization, blend-in eating a few cm,
slope, tiny speed variance). Each frame:

```
d   = |StopTarget − ActorPos|₂D          (clamped to 0 once passed/reached)
t' = clamp( C⁻¹(d),  t + ρ_min·Δt,  t + ρ_max·Δt ),   t' ≥ t,  t' ≤ PlayLength
```

- `ρ_min > 0` guarantees the stop always finishes even if the character is physically
  blocked (distance stops shrinking, time still advances).
- `ρ_max` bounds how hard the anim may fast-forward if distance collapses faster than
  authored (e.g. matched onto a steeper section).
- Monotonicity (`t' ≥ t`) prevents rewinds, which both look wrong and would extract
  backwards root motion.
- Default window `(0.6, 1.4)` keeps the pose within ~±40 % of authored timing — tight
  enough to be invisible, loose enough to converge in a few frames.

### 4.4 Late-trigger recovery

If the first evaluation already finds `R < D` for *every* entry (repath mid-run, move
issued at close range, corner deferral), a normal trigger is impossible — the window was
missed before it opened. Recovery: pick the smallest-distance entry and **enter it
mid-animation** at `t₀ = C⁻¹(R)`, so the anim's *remaining* slide equals the actual
remaining distance. This is the piece that makes the system robust rather than merely
correct on the happy path — the same lookup that drives the closed loop doubles as an
arbitrary-entry-point solver.

### 4.5 AI-request handshake

Root motion must own the capsule during the stop, and the Behavior Tree must not see a
failed move:

```
trigger:    PathFollowing.PauseMove(request, VelocityMode=Reset)
              → no more acceleration injected; root motion drives; request stays alive
complete:   PathFollowing.ResumeMove(request)
              → goal already reached (we stopped ON it) → request finishes SUCCESS
              → BT MoveTo succeeds with zero custom plumbing
external abort mid-stop (BT branch change, new move) → CancelStop(), ABP releases
```

Completion is signaled explicitly (AnimNotify at end of stop / ABP state exit →
`NotifyStopFinished()`), with a timeout safety net so a lost notify can never park the AI
in the stopping state forever.

## 5. Braking-based prediction (players / no path)

For a capsule-driven character (player input, no nav path) the equivalent of `R` comes
from predicting the braking stop distance. `UCharacterMovementComponent::ApplyVelocityBraking`
integrates `v' = −f·v − d` (`f` = effective friction × factor, `d` = braking deceleration).
Solving:

```
v(t) = (v₀ + d/f)·e^(−f·t) − d/f            stop at  t_s = (1/f)·ln(1 + f·v₀/d)

StopDistance = ∫₀^{t_s} v dt = v₀/f − (d/f²)·ln(1 + f·v₀/d)
```

with analytic limits `f→0 ⇒ v₀²/2d` (constant decel) and `d→0 ⇒ v₀/f` (pure exponential
decay). Implemented as `PredictGroundMovementStopDistance` — feed it your
CharacterMovement values; sanity-check limits: `d→∞ ⇒ 0` ✓, matches Epic's
`PredictGroundMovementStopLocation` behavior.

## 6. Architecture & threading

```
UStopAnimSetAsset (data)            stop anims + gait/direction/speed metadata
        │ load time (game thread)
        ▼
UAdvancedDistanceMatchingLibrary    curve extraction  →  FDistanceCurveCache per entry
        │
        ▼
UPredictiveStopComponent (game thread, TG_PrePhysics)
   plan / monitor / trigger / closed-loop time / AI handshake
        │ publishes plain properties:
        │   bStopActive, ActiveStopAnim, ActiveCurve,
        │   DistanceToStopTarget, MatchedStopTime, RecommendedGaitName
        ▼ Property Access (proxy copy — thread-safe by construction)
Animation Blueprint (worker thread)
   state transition on bStopActive; Sequence Evaluator driven either by
   the component's MatchedStopTime (simplest) or by the library's
   thread-safe driver functions (DistanceMatchStopToTarget)
```

Curve **building** is game-thread-only (samples animation data). Curve **lookups** are
pure and allocation-free — safe from the anim worker thread, which is why the driver
functions can run inside anim node functions, mirroring Epic's plugin design.

## 7. Edge cases & failure modes

| case | handling |
|---|---|
| path shorter than directional stop | plan phase → QuickStop / IdleOrStep + gait cap (§4.1) |
| repath mid-run inside stop distance | late-trigger recovery, mid-anim entry (§4.4) |
| corner within stop range near goal | corner block: fit-in-segment or defer (§4.2) |
| trigger frame quantization | early-bias lookahead → lands short, never long (§4.2) |
| character physically blocked mid-stop | `ρ_min` keeps time advancing; stop completes (§4.3) |
| overshoot past target mid-stop | remaining clamped to 0 via passed-detection; anim runs out |
| finished-notify lost | timeout = remaining length × 1.5 + slack → force complete |
| BT aborts move mid-stop | `OnRequestFinished` → `CancelStop()`, ABP releases |
| controller/possession changes | path-following delegate re-bound on resolve |
| stop anim with no root translation | curve build fails loudly at load, entry skipped |
| speed at trigger ≠ entry ReferenceSpeed | nearest-speed entry selection; root motion keeps the distance deterministic regardless |

## 8. Alternatives considered

- **Epic's AnimationLocomotionLibrary plugin** — solves matching but not path-aware
  triggering (the actual bug), assumes capsule-driven stops and authored curves. We keep
  its proven API shape and own the tech.
- **Motion Warping** — warps root motion to land on a target; complementary, and a good
  add-on for stops on slopes, but it rescales the authored slide (changes its feel) and
  still needs the *predictive trigger + short-path planning* this system provides.
- **Shrinking the navmesh from edges** (nav modifiers / agent radius padding) — treats the
  symptom globally, costs walkable area everywhere, and still stops *late* on open ground.
  Worth keeping as a defense-in-depth layer, not a fix.
- **Acceptance-radius hacks** — finishing the move early by `D` via acceptance radius
  reports success at the trigger point, letting the BT issue the next move while the stop
  is still sliding. The pause/resume handshake avoids that race.
