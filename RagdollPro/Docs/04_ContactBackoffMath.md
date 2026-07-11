# 04 — Contact Backoff Math: The Decaying Impulse Accumulator

RagdollPro softens the joint motors the moment the body is in "complex contact" (a pileup, a
stack of props, another ragdoll) and restores them when things calm down. The sensor for this is
a **leaky integrator** over collision impulses. This doc derives its behavior, its frame-rate
independence, and how to tune the three knobs.

## 1. The signal: accumulated contact impulse

Every physics contact reported through `OnComponentHit` carries a normal impulse
$J_i = \lVert \text{NormalImpulse} \rVert$ (units: mass × velocity, e.g. kg·cm/s). A single
number per hit is a noisy, spiky signal:

- a clean landing produces one or two large spikes, then silence;
- a pileup produces a *sustained stream* of small-to-medium impulses as bodies jostle.

We want to distinguish those two cases. Neither the peak value (both cases spike) nor the
instantaneous value (chatters frame to frame) works. What separates them is **impulse rate over
recent time** — which is exactly what a leaky integrator measures.

## 2. The leaky integrator

Define the accumulator $A(t)$ with decay rate $\lambda$ (`ContactImpulseDecayRate`, 1/s):

$$
\frac{dA}{dt} = -\lambda A + \sum_i J_i\,\delta(t - t_i)
$$

i.e. continuous exponential decay plus an instantaneous bump per contact. Between contacts the
solution is pure decay:

$$
A(t) = A(t_0)\,e^{-\lambda (t - t_0)}
$$

### Discrete implementation

Per tick of length $\Delta t$ (in `TickComponent`):

```cpp
ContactAccumulator *= FMath::Exp(-ContactImpulseDecayRate * DeltaTime);
```

and per hit event (in `HandleMeshHit`):

```cpp
ContactAccumulator += NormalImpulse.Size();
```

### Frame-rate independence (why `exp`, not `A *= (1 - λΔt)`)

Composing the exact update over two half-steps:

$$
A \cdot e^{-\lambda \Delta t_1} \cdot e^{-\lambda \Delta t_2} = A \cdot e^{-\lambda(\Delta t_1 + \Delta t_2)}
$$

The exponential form gives **identical decay for any tick partitioning** — 30 FPS, 144 FPS, or
hitchy frame times all produce the same $A(t)$ envelope. The common shortcut
$A \mathrel{*}= (1 - \lambda\Delta t)$ is only the first-order Taylor expansion of
$e^{-\lambda\Delta t}$; it under-decays at low frame rates, over-decays at high ones, and goes
*negative* (!) if a hitch makes $\lambda \Delta t > 1$. Exact exponentials cost one `exp` per
tick and remove an entire class of frame-rate-dependent tuning bugs.

(The `if (A < 1) A = 0` flush is a denormal/noise floor: exponential decay never actually
reaches zero, and there is no reason to keep multiplying a meaningless 10⁻⁶ forever.)

## 3. Steady-state analysis: what the threshold means

Suppose contacts arrive as an approximately steady stream with total impulse rate
$R$ (impulse per second — e.g. many small hits in a pileup). The accumulator converges to the
fixed point of $\frac{dA}{dt} = -\lambda A + R = 0$:

$$
\boxed{\;A_{ss} = \frac{R}{\lambda}\;}
$$

So the trigger condition `A > ContactImpulseThreshold` ($A > T$) is really a condition on
**sustained impulse rate**:

$$
R > \lambda\,T
$$

With defaults $\lambda = 4$/s and $T = 300$: sustained contact impulse above ~1200 units/s
counts as "complex contact".

Conversely a **single spike** $J$ (clean landing) decays as $J e^{-\lambda t}$ and stays above
threshold only for

$$
t_{above} = \frac{1}{\lambda}\ln\frac{J}{T}
$$

A landing spike of $J = 3T$ keeps the motors backed off for
$\ln(3)/4 \approx 0.27$ s — a brief, appropriate softening on touchdown, versus indefinitely for
a pileup that keeps feeding the integrator. **This asymmetry — brief response to spikes,
sustained response to streams — is the entire point of the design.**

### Half-life intuition for tuning $\lambda$

$$
t_{1/2} = \frac{\ln 2}{\lambda} \approx \frac{0.69}{4} \approx 0.17\ \mathrm{s} \ \text{(default)}
$$

Raise $\lambda$ → shorter memory → motors recover faster after contact stops. Lower $\lambda$ →
the body stays soft longer after the last touch.

## 4. From sensor to actuation: smoothed multiplier

The raw trigger is binary (above/below threshold). Feeding a step function into motor strength
would pop. The applied multiplier chases its target exponentially
(`FMath::FInterpTo`, rate $\rho$ = `DriveMultiplierInterpSpeed`):

$$
m_{t+1} = m_t + (m_{target} - m_t)\,\min(\rho\,\Delta t,\, 1), \qquad
m_{target} = \begin{cases} m_{contact} & A > T \\ 1 & A \le T \end{cases}
$$

This is a first-order low-pass filter with time constant $1/\rho$ ($\approx 0.17$ s at the
default $\rho = 6$): after a threshold crossing the multiplier covers ~63% of the gap in
$1/\rho$ seconds, ~95% in $3/\rho$. Two smoothing stages total (integrator memory + actuation
low-pass) means a single noisy contact frame moves the motors almost not at all — and doc 03 §4
guarantees that whatever strength this multiplier lands on, the *character* of the joint motion
(damping ratio) is unchanged.

## 5. State gating

Hits are only accumulated during `BlendingIn` and `Active`:

```cpp
if (State != Active && State != BlendingIn) return;
```

- Before `Start*()` the mesh isn't simulating; any reported hits are kinematic sweep artifacts.
- During `BlendingOut` the animation is taking over; softening the motors then would fight the
  handover (the blend already relaxes them — doc 06).

## 6. Tuning procedure

1. Set `bDrawDebug = true` — the accumulator value is printed at the pelvis.
2. Do a **clean landing** on flat ground. Note the peak $A$ (call it $A_{clean}$).
3. Do a **worst-case pileup** (drop 4–5 ragdolls onto each other). Note the sustained plateau
   $A_{pile}$ (that's your measured $R/\lambda$).
4. Set `ContactImpulseThreshold` between them, biased toward the clean value:
   $T \approx A_{clean} \cdot 1.5$, sanity-check $T < 0.7\,A_{pile}$.
5. If motors visibly pump on/off near the threshold (limit cycling), widen the gap by raising
   `ContactImpulseDecayRate` (steadier plateau separation) or lowering
   `DriveMultiplierInterpSpeed` (slower actuation hides the chatter). A hysteresis band
   (separate on/off thresholds) is on the roadmap (doc 08) if limit cycling shows up in the
   field.
6. `ContactDriveMultiplier = 0.1` means "10% muscle tone in a pile". `0` is fully passive —
   physically most correct, but limbs then splay freely; keep a little tone unless you want
   full unconscious-body behavior.
