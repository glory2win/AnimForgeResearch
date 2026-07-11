# 07 — animforge_core (AnimForge.Core)

The math core. Pure C#, `System.Numerics` only — no engine, no UI, no I/O. The API is the
contract; the implementation can later become P/Invoke into a native `animforge_core`
without touching a single caller. Import style:

```csharp
using static AnimForge.Core.All;   // "animforge_core.all"
```

## Springs & dampers (`Maths/SpringDamper.cs`)

### Why "exact" integrators

The damped spring ODE is `x'' = -ω²(x−g) − 2y·x'`. Explicit Euler on this explodes when
`dt` is large relative to the stiffness. Instead we evaluate the **closed-form solution**
for the elapsed `dt` — unconditionally stable, frame-rate independent, and cheap.

### Parameterization

Animators don't think in spring constants, so:

* **halflife** — seconds until the remaining error halves. The decay rate is
  `y = 4·ln2 / halflife` (the 4 makes "halflife" visually match when the value is ~settled,
  a convention from Daniel Holden's spring articles, which these functions follow).
* **frequency** — oscillations per second, `ω = 2π·f`.

### The three regimes of `SpringExact`

With decay `y` and angular frequency `ω` (comparing `ω` vs `y`):

| Regime | Condition | Solution shape |
|---|---|---|
| Critically damped | `ω ≈ y` | `x(t) = (j0 + j1·t)·e^(−y·t) + g` — fastest non-overshooting |
| Under-damped | `ω > y` | `e^(−y·t)·(j0·cos(ωd·t) + j1·sin(ωd·t)) + g`, `ωd = √(ω²−y²)` — bouncy |
| Over-damped | `ω < y` | sum of two exponentials with rates `y ± √(y²−ω²)` — sluggish |

`j0`, `j1` come from the initial conditions (current offset and velocity). Velocity is
updated from the analytic derivative, so chaining calls with varying `dt` stays exact.

`DamperExact` is the critically-damped special case — the workhorse for smoothing
positions, camera targets, blend weights. `FastNegExp` is a rational approximation of
`e^-x` (max error ~0.2% on the useful range) so there's no `MathF.Exp` in the hot path.

`MeasureOvershoot` step-tests a configuration and returns peak overshoot — this is what
the Heatmap panel visualizes across the (frequency, halflife) plane.

### Rules of thumb

* Smoothing UI/camera: `DamperExact`, halflife 0.05–0.3 s.
* Secondary motion (antenna, tail): `SpringExact`, freq 1–4 Hz, halflife 0.2–0.8 s.
* Never integrate these with accumulated error in mind — they're exact; call once per frame
  with the real `dt`, including variable `dt`.

## Two-bone IK (`Maths/TwoBoneIk.cs`)

Analytic law-of-cosines solver in **position space**: input three joint positions
(root/mid/end), a target, and a pole hint; output new mid/end positions.

Derivation sketch: with bone lengths `a` (root→mid), `b` (mid→end) and clamped target
distance `d`, the mid joint lies on a circle around the root→target axis. Its axial
offset is `p = (d² + a² − b²) / 2d` (law of cosines) and radial offset `h = √(a² − p²)`.
The pole hint, projected perpendicular to the axis, picks the point on that circle —
i.e. the bend plane. Degeneracies handled: target clamped to `[|a−b|+ε, a+b−ε]` (never
fully straight → no elbow pop), pole falling on the axis → arbitrary perpendicular.

Converting to joint **rotations** for a specific skeleton = aim each bone at its new child
position while preserving roll; that is rig-convention-specific and intentionally left to
the caller (the JacobianDLSIK module covers the general-chain case).

## Distance matching (`Maths/DistanceMatching.cs`)

`DistanceCurve` = monotonic time→distance samples (build with `FromDisplacements`).
Two queries:

* `DistanceAtTime(t)` — linear interp.
* `FindTimeAtDistance(d)` — the inverse, binary search O(log n). This is the core of
  distance-matched locomotion (pick the animation time whose remaining distance to the
  stop point matches the predicted braking distance).

Same primitive as the AdvancedDistanceMatching research module — kept here so tools can
plot/inspect the curves.

## Curves (`Maths/Curves.cs`)

`SmoothStep01`, `EaseInOutCubic`, `Remap` (clamped), cubic `Hermite` (basis form),
`CatmullRom` for trajectory smoothing/preview.

## Visualization (`Viz/`)

* `Colormap` — viridis (default; perceptually uniform, colorblind-safe) and coolwarm
  (diverging, for signed fields) as 8–11 stop LUTs with linear interp. `ToRgba` packs for
  ImGui draw lists.
* `HeatmapGrid` — W×H float field, `Fill(Func<u,v,float>)`, min/max tracking,
  `Normalized(x,y)` for colormap input. UI-toolkit-agnostic on purpose.

## Testing note

Everything here is deterministic and dependency-free — add an `AnimForge.Core.Tests`
xunit project as the first CI step (spring: converges to goal, no overshoot when critical;
IK: reach clamps, bend follows pole; distance: inverse round-trips).
