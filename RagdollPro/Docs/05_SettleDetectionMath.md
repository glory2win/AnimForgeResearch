# 05 — Settle Detection Math: Rolling Windows, Hysteresis, and Why Physics Sleep Is Not Enough

The blend-out decision — "the body has come to rest, hand control back to animation" — sounds
trivial and is one of the most common sources of shipped ragdoll bugs: corpses that wobble
forever, or blend-outs that fire mid-tumble. This doc derives RagdollPro's detector.

## 1. Why not the engine's sleep flag?

Chaos puts a body to sleep when its (smoothed) kinetic energy stays under a threshold for a
number of frames. Three failure modes for ragdolls:

1. **Resting-contact chatter.** A multi-body chain in contact with the ground exchanges tiny
   correction impulses every solve. Any one body can be repeatedly nudged above the sleep
   threshold by its neighbors — an *island* of 15 bodies effectively never sleeps in messy
   contact configurations. The result: the blend-out never fires.
2. **All-or-nothing.** Sleep is per body, and waking any body wakes the island. There is no
   "settled enough for gameplay" notion.
3. **Opaque tuning.** Sleep thresholds are per physics material / solver settings, far from
   where a gameplay programmer looks.

RagdollPro therefore makes its own judgment from sampled velocities, with three independent
defenses against noise: **normalization, windowing, and sustain hysteresis** — plus a hard
timeout as the last resort.

## 2. The instantaneous statistic: normalized worst-case speed

Per tick, over a small set of `KeyBones` $B$ (pelvis, head, hands, feet — extremities jitter
first and are the cheapest tell):

$$
v^* \;=\; \max_{b \in B}\;\max\!\left(
    \frac{\lVert \mathbf{v}_b \rVert}{v_{lin}},\;
    \frac{\lVert \boldsymbol{\omega}_b \rVert}{v_{ang}}
\right)
$$

where $v_{lin}$ = `SettleLinearVelocityThreshold` (cm/s) and $v_{ang}$ =
`SettleAngularVelocityThreshold` (deg/s).

Design decisions packed into this one line:

- **Normalization** puts linear and angular speeds — different units, different scales — on a
  common dimensionless axis where **1.0 means "at its threshold"**. The rest of the pipeline
  never needs to know about units again, and the two thresholds remain independently tunable.
- **Max, not mean, across bones.** A body is settled only if *every* sampled bone is settled. A
  mean would let a violently twitching hand be averaged away by five still bones. (Max across
  bones, mean across *time* — the two aggregations answer different questions.)
- **Angular check matters independently:** a body pivoting about a planted extremity can have
  near-zero linear velocity at the pelvis while still visibly rotating.

## 3. Temporal smoothing: the rolling mean

$v^*$ is chattery (contact impulses arrive as spikes). It is pushed into a fixed-size ring
buffer of $N$ = `SettleWindowSize` samples and averaged:

$$
\bar{v}_t = \frac{1}{N}\sum_{i=0}^{N-1} v^*_{t-i}
$$

Properties of the moving-average filter:

- It is a low-pass FIR filter with first spectral null at $f = f_{tick}/N$; single-frame spikes
  are attenuated by a factor of $N$ (a lone spike of height $h$ moves the mean by only $h/N$).
- **Worst-case latency is $N$ ticks** — after motion truly stops, the mean needs up to $N$
  samples to drain the old motion out of the window. At $N = 12$ and 60 Hz, that's 200 ms —
  invisible next to the ~1–2 s a body takes to physically settle.
- The buffer is **seeded with 1.0** ("not settled") so the first $N$ ticks after `Start*()` can
  never spuriously report settled while the window is still filling. Implementation is a ring
  buffer — O(1) per tick, zero heap churn.

The settled predicate is then simply $\bar{v}_t \le 1$ — i.e. *on recent average, every key
bone is under its thresholds.*

## 4. Sustain hysteresis

A single below-threshold average isn't trusted either. The predicate must hold continuously:

$$
\text{settle fires at } t \iff \bar{v}_{t'} \le 1 \;\; \forall\, t' \in [t - T_s,\, t]
$$

with $T_s$ = `SettleSustainDuration` (0.25 s default). Implementation is a timer that resets to
zero on any violating tick.

This is **temporal hysteresis**: it converts "momentarily quiet" into "quiet and staying
quiet". The combination is deliberately redundant in different failure dimensions:

| Stage | Kills |
|---|---|
| Normalized max | unit mismatches, single-bone twitching hidden by averaging across bones |
| Rolling mean (N samples) | single-frame spikes, solver chatter |
| Sustain timer ($T_s$) | slow oscillations that dip below threshold once per cycle (a rocking body crosses "slow" twice per rock, but never *stays* slow) |

The slowest oscillation the sustain timer can reject has period $\approx 2T_s$; anything
rocking slower than that *and* dipping under the velocity thresholds is, for gameplay purposes,
actually settled.

## 5. The hard timeout

Some configurations never settle: a body draped over a ledge balancing on a knife-edge contact,
a foot caught in geometry, a physics prop jittering against the corpse forever.

$$
\text{blend-out also fires at } t_{Active} \ge T_{stuck} \quad (= 4\ \mathrm{s\ default})
$$

This is a **liveness guarantee**: the state machine provably reaches `BlendingOut` within
$T_{stuck}$ of entering `Active`, no matter what the physics does. Gameplay code downstream
(loot, despawn, getup) can rely on `OnSettled`/`OnFinished` always arriving. A detector with no
timeout is a hang waiting to happen; a timeout with no detector blends out mid-tumble. You need
both, and whichever fires first wins.

(Hit reactions skip settle detection entirely and recover on their own timer — the branch is
powered and tracking animation the whole time, so "settled" is not a meaningful question.)

## 6. Failure-mode table for tuning

| Symptom | Knob | Direction |
|---|---|---|
| Blend-out fires while body still visibly moving | `SettleLinearVelocityThreshold` / `SettleAngularVelocityThreshold` | lower |
| Corpse takes ages to hand back control on clean falls | thresholds | raise |
| Settle flickers near rest (fires, then motion resumes) | `SettleSustainDuration` | raise (0.35–0.5) |
| Detection feels laggy after true rest | `SettleWindowSize` | lower (8) |
| Bodies stuck on props never blend out | `StuckTimeout` | lower |
| Blend-out cuts off long dramatic tumbles | `StuckTimeout` | raise |

Default sanity numbers: $v_{lin} = 40$ cm/s, $v_{ang} = 60$ °/s, $N = 12$, $T_s = 0.25$ s,
$T_{stuck} = 4$ s. Total worst-case natural-settle latency after true rest:
$N/f_{tick} + T_s \approx 0.45$ s at 60 Hz.
