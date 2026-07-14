# JacobianDLSIK

Research project: a Jacobian **Damped Least Squares** IK solver for Unreal
Engine — arbitrary bone chains, per-joint weights, swing/twist limits, and
*provably bounded* behavior at singular poses (the straight-leg / full-reach
case where CCD and pseudoinverse solvers jitter and snap).

**The problem this solves:** UE's chain solvers each break somewhere. Two Bone
IK is exact but stops at 2 bones. CCD curls chains tip-heavy and is
order-dependent. FABRIK solves positions and back-derives rotations, so twist is
undefined and joint limits are hacks. And *every* undamped method inherits the
same math bomb: near a singular pose the required joint step scales as 1/σ → ∞,
which players see as knee jitter and elbow pops. **The fix:** damped least
squares changes the objective — minimize task error *plus* λ²·(joint motion) —
which caps the response at 1/(2λ) everywhere, by construction. Implemented here
in a form that needs only **O(N) work and a single 3×3 solve per iteration**,
with adaptive damping that engages only near singularities.

## Files

| file | contents |
|---|---|
| [THEORY.md](THEORY.md) | **read first** — the full math from first principles, structured as our pipeline: 1 Concept Overview → 2 Data Flow → 3 Algorithm (Jacobian, SVD, singularities, DLS derivation, O(N) identity, damping, weights, limits) → 4 Pseudocode → 5 C++ map → 6 Review (a one-iteration numeric trace you can check by hand) |
| [JacobianDLSIK_Slides.pptx](JacobianDLSIK_Slides.pptx) | presentation version of the theory — the whole story in ~15 slides with the diagrams |
| [DESIGN.md](DESIGN.md) | architecture, solve-loop flowchart, decisions defended (JJᵀ form, Cholesky, linearization-point discipline), threading/memory, perf profile, edge cases, extensions (6-DOF, multi-effector, SDLS, nullspace) |
| [INTEGRATION.md](INTEGRATION.md) | Build.cs, file placement, AnimBP wiring, tuning cheat sheet, debug CVar, running the tests |
| [TESTING.md](TESTING.md) | test plan: what each automated test proves (mapped to THEORY.md sections), manual singularity-torture and slope-walk scenarios, honest perf-comparison methodology |
| [JacobianDLSSolver.h](JacobianDLSSolver.h) / [.cpp](JacobianDLSSolver.cpp) | the core solver — pure math, engine-agnostic, unit-testable |
| [AnimNode_JacobianDLSIK.h](AnimNode_JacobianDLSIK.h) / [.cpp](AnimNode_JacobianDLSIK.cpp) | skeletal control node (runtime module): chain caching, space conversion, per-bone settings, debug draw |
| [AnimGraphNode_JacobianDLSIK.h](AnimGraphNode_JacobianDLSIK.h) / [.cpp](AnimGraphNode_JacobianDLSIK.cpp) | AnimGraph palette node (editor module) |
| [Tests/JacobianDLSSolverTests.cpp](Tests/JacobianDLSSolverTests.cpp) | 8 automation tests: finite-difference Jacobian check, convergence, **damping-bounds-singular-step**, unreachable stability, limits, locking, determinism, perf |
| [Diagrams/](Diagrams) | [fk_vs_ik](Diagrams/fk_vs_ik.svg) · [jacobian_geometry](Diagrams/jacobian_geometry.svg) · [singularity](Diagrams/singularity.svg) · [dls_gain_curve](Diagrams/dls_gain_curve.svg) · [solve_loop](Diagrams/solve_loop.svg) |

## Quick start

1. Copy the solver + anim node pairs into your runtime module, the graph node
   pair into your editor module; replace `YOURGAME_API`; add `AnimGraphRuntime`
   (runtime) and `AnimGraph`/`BlueprintGraph`/`UnrealEd` (editor) to Build.cs.
2. Run the automation tests (`AnimForge.JacobianDLSIK`) — green before anything else.
3. AnimGraph: add **Jacobian DLS IK**, set RootBone/TipBone (e.g.
   `upperarm_l`→`hand_l`), drive EffectorLocation from a pin.
4. `a.AnimNode.JacobianDLSIK.Debug 1` to see chain/target/effector; the AnimBP
   debugger shows `iters/err/lambda/iso` live.
5. Tune per the cheat sheet in INTEGRATION.md §3 (defaults fit a ~60 cm arm).

## The one-line math summary

```
Δθ = Jᵀ·(J·Jᵀ + λ²I)⁻¹·e         with   J·Jᵀ = Σᵢ wᵢ·(‖rᵢ‖²I − rᵢrᵢᵀ)
                                  and    ωᵢ  = wᵢ·(rᵢ × y)   per-joint update
```

Per-direction gain σ/(σ²+λ²) instead of 1/σ: identical to the pseudoinverse far
from singularities, smoothly bounded at them. The two identities on the right
are why the whole thing runs in O(N) with one 3×3 Cholesky — derived in
THEORY.md §3.6–3.7.

Targets UE 5.3–5.6, stock engine modules only.
