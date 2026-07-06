# Advanced Distance Matching — Integration Guide

Step-by-step wiring. Read [DESIGN.md](DESIGN.md) first for what each piece does and why.

---

## 1. Add the code to your project

1. Copy the four file pairs into your game module's source folder:
   - `DistanceMatchingTypes.h/.cpp`
   - `AdvancedDistanceMatchingLibrary.h/.cpp`
   - `PredictiveStopComponent.h/.cpp`
2. Replace every `YOURGAME_API` with your project's API macro (e.g. `MYGAME_API`).
3. Add module dependencies in `YourGame.Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine",
    "AIModule",            // AAIController, UPathFollowingComponent
    "NavigationSystem",    // nav path types
    "AnimGraphRuntime"     // FSequenceEvaluatorReference + anim node function support
});
```

All stock engine modules — **no plugin required**. `AnimationLocomotionLibrary` stays
disabled.

**Engine version notes** (single call sites, easy to adjust):
- `ExtractRootMotionRange` in `AdvancedDistanceMatchingLibrary.cpp` wraps
  `UAnimSequence::ExtractRootMotionFromRange(float, float)` — stable UE4 → UE 5.5. If your
  version deprecated it for the `FAnimExtractContext` overload, swap it there.
- Same for `HasCurveData` / `EvaluateCurveData(FName, float)` in
  `BuildDistanceCurveFromAuthoredCurve`.

## 2. Animation asset checklist

For every stop animation you register:

- [ ] **Enable Root Motion** is on (the authored slide must drive the capsule — this is
      your existing working setup).
- [ ] Root motion actually reaches the capsule: ABP root motion mode is
      *Root Motion from Everything* (state-machine stops) or the stop plays in a montage
      slot (*Root Motion from Montages*).
- [ ] A **"Predictive Stop Finished"** AnimNotify sits at (or a few frames before) the
      end of the sequence. Without it the component falls back to a timeout — works, but
      the resume handshake is late.
- [ ] Optional: an authored `Distance` float curve if you prefer hand-authored matching
      data over root-motion extraction (set `bUseAuthoredDistanceCurve` on the entry).

## 3. Create the stop set data asset

Content Browser → Miscellaneous → Data Asset → `StopAnimSetAsset`. Example:

| StopAnim | GaitName | ReferenceSpeed | Direction | bQuickStop |
|---|---|---|---|---|
| `A_Stop_Sprint_F` | Sprint | 600 | Forward | ☐ |
| `A_Stop_Jog_F` | Jog | 350 | Forward | ☐ |
| `A_Stop_Jog_L` | Jog | 350 | Left | ☐ |
| `A_Stop_Jog_R` | Jog | 350 | Right | ☐ |
| `A_Stop_Walk_F` | Walk | 170 | Forward | ☐ |
| `A_QuickStop_F` | Jog | 350 | Forward | ☑ |

Notes:
- `GaitName` is *your* label — the component only groups and sorts by `ReferenceSpeed`.
- One Forward entry per gait is a fine start for AI (path followers face their motion).
- Quick stops are the short-path fallback tier; author at least one.

## 4. Add the component

Add `PredictiveStopComponent` to your AI character, assign the data asset. On BeginPlay
it builds all distance curves and logs each one:

```
LogPredictiveStop: BP_Enemy_C_1: registered stop 'A_Stop_Sprint_F' (gait Sprint, Forward) -- distance 285.3 cm over 1.27 s.
```

**Verify these logged distances against your animations before anything else** — if the
extracted distance looks wrong (0, or meters off), fix root motion on the asset first.

Key config (defaults are sensible):

| property | default | meaning |
|---|---|---|
| `TriggerLookaheadFrames` | 1.0 | early-trigger bias; higher = land shorter/safer |
| `GaitCommitMargin` | 150 | extra path length (cm) a gait needs beyond its stop distance — must cover acceleration-to-gait distance |
| `CornerCutTolerance` | 25 | along-path vs straight-line disagreement (cm) that flags a corner |
| `PlayRateClamp` | (0.6, 1.4) | closed-loop time scrub window |
| `bPauseMoveOnTrigger` / `bResumeMoveOnComplete` | true | the AI-request handshake |
| `bDrawDebug` | false | goal sphere + trigger-distance ring + live stop readout |

## 5. Animation Blueprint wiring

Two levels of adoption. **Phase 1 already fixes the ledge bug**; Phase 2 makes the
landing exact and handles late triggers. Ship Phase 1 first, verify, then add Phase 2.

Expose the component's outputs to the ABP the explicit, always-safe way: in
`NativeUpdateAnimation` (or the BP Event Graph) copy what you need into anim instance
variables — `bStopActive`, `ActiveStopAnim`, `ActiveCurve`, `DistanceToStopTarget`,
`MatchedStopTime`. (Property Access bindings directly to the component also work; reads
of external objects are proxy-copied on the game thread.)

### Phase 1 — open loop (trigger fix only, minimal ABP change)

Your existing stop states stay as they are. Only the *transition* changes: enter the
stop state when `bStopActive` is true (instead of your current goal-reached/velocity
condition), and play `ActiveStopAnim`. Root motion does the rest — the trigger timing
alone puts the slide's end on the goal.

Exit the stop state when `bStopActive` goes false (the finished notify → component →
`CompleteStop` chain clears it), transitioning into Idle.

### Phase 2 — closed loop (exact landing + late-trigger entry)

Replace the stop state's Sequence Player with a **Sequence Evaluator**, then either:

**Option A — component-computed time (simplest, no anim node functions):**
bind the evaluator's *Explicit Time* pin to your copied `MatchedStopTime`. Done — the
component already runs the full clamped matching loop every tick.

**Option B — in-graph driver functions (Epic-plugin style):**
leave *Explicit Time* unexposed and add anim node functions on the evaluator node:
- *On Initial Update* → `InitializeStopEvaluator(Evaluator, ActiveStopAnim, ActiveCurve,
  DistanceToStopTarget)` — sets the sequence and the distance-matched start time
  (mid-anim on late triggers).
- *On Update* → `DistanceMatchStopToTarget(Context, Evaluator, ActiveCurve,
  DistanceToStopTarget, PlayRateClamp)`.

**Evaluator checklist (both options):**
- [ ] **Teleport to Explicit Time = FALSE** — otherwise no root motion is extracted and
      notifies (including the finished notify!) never fire. #1 gotcha.
- [ ] Option A: Explicit Time pin bound; never call the driver functions too (they'd
      fight the pin and log warnings).
- [ ] Option B: Explicit Time pin NOT exposed; Sequence pin NOT exposed (set via
      `InitializeStopEvaluator`).

### Montage alternative

If your AI stops run as montages instead: on `OnStopTriggered`, play the stop montage
with `Montage_Play(..., StartTimeSeconds = MatchedStartTime)`. That gives you open loop +
late-trigger recovery; skip per-frame position corrections (jittery on montages) — the
early-bias trigger keeps the raw error under one frame of travel anyway.

## 6. AI / Behavior Tree integration

With the default handshake you need **no BT changes**: the move request is paused during
the stop and resumed at completion, so the stock *Move To* task finishes with success
when the character is standing on the goal.

Optional hooks:
- `OnMovePlanEvaluated(Plan, RecommendedGait, PathLength)` — cap your gait here. E.g. set
  the blackboard/movement max speed to the recommended gait's speed so the AI never
  sprints on a path it can't stop from.
- `OnStopTriggered` / `OnStopCompleted` / `OnStopCanceled` — hook VFX, foot planting,
  or custom BT services.
- Keep BT MoveTo acceptance radius small (≤ ~20 cm). Large acceptance radii finish the
  request before the predictive trigger gets a chance (harmless — behavior is just the
  old one — but you lose the feature on those moves).

## 7. Debugging

- `bDrawDebug` on the component:
  - **Monitoring**: yellow sphere = goal, cyan ring = current candidate's authored stop
    distance around it. The character should visibly start its stop as it crosses the ring.
  - **Stopping**: green sphere/line to target + on-head `STOP <dist> t=<time>` readout.
- `LogPredictiveStop` at Verbose logs every trigger (chosen anim, remaining vs authored
  distance, matched start time) and every plan decision.

Common issues:

| symptom | cause |
|---|---|
| character stops dead at trigger, no slide | root motion not reaching capsule (root motion mode / montage slot) |
| stop anim frozen on first frame | evaluator's Teleport-to-Explicit-Time is true, or Explicit Time pin bound to a stale variable |
| `could not set explicit time` warnings | Explicit Time pin exposed while using driver functions (pick Option A **or** B) |
| stops trigger way too early/late | logged extracted distance wrong → root motion issue on the asset; or units (curve is cm) |
| AI stuck after stop | finished notify missing (timeout warning in log confirms) or `bResumeMoveOnComplete` off with a BT that waits on the request |
| sprint stops on short paths | gait cap not consuming `RecommendedGaitName`; or `GaitCommitMargin` smaller than your acceleration distance |

## 8. Test checklist

1. **Flat open ground, long path** — stop lands on the goal point (debug ring crossing =
   stop start). Compare against old behavior: previously landed ~stop-distance past.
2. **The ledge case** — goal near a railless platform edge, sprint approach: character
   must stop *on* the point, zero falls across ~50 runs.
3. **Short path** (< sprint stop distance) — plan logs QuickStop/IdleOrStep, gait capped,
   no directional sprint stop fires.
4. **Repath mid-run** — order a new move while sprinting such that the new goal is inside
   stop distance: late-trigger recovery enters the stop mid-anim (log shows start time > 0).
5. **Corner near goal** — path with a turn within stop distance of the goal: no corner
   cutting off the path edge; stop fires on the final segment.
6. **BT abort mid-stop** — switch BT branch during the slide: stop cancels, character
   responds to the new move immediately.
7. **Blocked mid-stop** — put a wall inside the slide: stop still completes (min play
   rate), no infinite Stopping state.
8. **Frame-rate sweep** — 30 / 60 / 120 fps: landing error stays sub-centimeter with
   closed loop; open loop error stays ≤ one frame of travel, always short.
9. **Notify removed** (regression guard) — timeout warning appears and the AI still
   resumes.
