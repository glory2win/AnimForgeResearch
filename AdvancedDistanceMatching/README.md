# AdvancedDistanceMatching

Custom C++ distance matching for predictive locomotion stops — no
`AnimationLocomotionLibrary` plugin required.

**The bug this fixes:** directional stops carry an authored root-motion slide. Path
following runs the character all the way to the AI goal point and only then plays the
stop, so the slide overshoots the point — and off railless platform edges, the character
falls. **The fix:** every stop's slide distance is extracted from its root motion at load
time; the stop is triggered early, when `remaining path distance ≤ stop distance`, so the
slide ends exactly on the goal. Short paths that can't fit a directional stop degrade to
quick stop / idle, with a recommended gait cap broadcast to the movement system.

## Files

| file | contents |
|---|---|
| [DESIGN.md](DESIGN.md) | theory, the formalized algorithm, math, edge cases, alternatives |
| [INTEGRATION.md](INTEGRATION.md) | Build.cs, asset checklist, ABP wiring, BT integration, debugging, test plan |
| [DistanceMatchingTypes.h](DistanceMatchingTypes.h) / .cpp | `FDistanceCurveCache` (distance ↔ time lookups), stop entry struct, `UStopAnimSetAsset`, enums |
| [AdvancedDistanceMatchingLibrary.h](AdvancedDistanceMatchingLibrary.h) / .cpp | curve extraction from root motion or authored curves, thread-safe matching functions, Sequence Evaluator drivers, braking-distance prediction |
| [PredictiveStopComponent.h](PredictiveStopComponent.h) / .cpp | the brain: path monitoring, move planning (short-path sub-case), predictive trigger, closed-loop matched time, AI pause/resume handshake, debug draw + `UAnimNotify_PredictiveStopFinished` |

## Quick start

1. Copy the 3 file pairs into your module, replace `YOURGAME_API`, add `AIModule`,
   `NavigationSystem`, `AnimGraphRuntime` to Build.cs.
2. Create a `StopAnimSetAsset` listing your stop anims (gait, direction, reference speed;
   flag quick stops).
3. Add `PredictiveStopComponent` to the AI character, assign the asset. Check the logged
   extracted stop distances look right.
4. Put the **Predictive Stop Finished** notify at the end of each stop anim.
5. ABP: enter your stop state on `bStopActive`, play `ActiveStopAnim`. → **ledge bug
   fixed** (Phase 1, open loop).
6. Optional Phase 2 (exact landing + late-trigger recovery): swap the stop state's player
   for a Sequence Evaluator driven by `MatchedStopTime` (or the library's driver
   functions). Details in INTEGRATION.md §5.

## Design highlights

- **Early-bias trigger** `R ≤ D + v·Δt` — quantization error always lands *short* of the
  goal, never past it (never off the ledge).
- **Closed-loop matching** — evaluator time driven by live distance-to-target through the
  extracted curve, play-rate clamped `(0.6, 1.4)`, monotonic. Exact landings; stable even
  though root motion itself drives the capsule.
- **Late-trigger recovery** — repath inside stop distance? Enter the stop mid-anim at
  `C⁻¹(R)` so the remaining slide equals the remaining distance.
- **Move planning** — per-path classification (DirectionalStop / QuickStop / IdleOrStep)
  + fastest-safe-gait recommendation: your "path shorter than the stop" sub-case.
- **AI handshake** — PauseMove at trigger, ResumeMove at completion → BT MoveTo succeeds
  naturally, no custom tasks.

Targets UE 5.3–5.5 (two clearly-marked engine API call sites to adjust for other
versions). Uses only stock engine modules.
