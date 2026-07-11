# 03 — Spring-Damper Theory: The Harmonic Oscillator Behind Every Joint

Every RagdollPro joint is a damped torsional harmonic oscillator. This doc derives its behavior
from the equation of motion up to the one non-obvious design decision in the code: **why damping
is scaled by the square root of the strength scale** (`DampingScale = sqrt(Scale)` in
`ApplyMotorStrength`).

## 1. Equation of motion

Take one joint, one correction axis (the SLERP error axis from doc 02). Let $\theta(t)$ be the
angular error, $I$ the child body's moment of inertia about that axis, $k$ the drive spring and
$c$ the drive damping. Newton's second law for rotation:

$$
I\ddot{\theta} + c\dot{\theta} + k\theta = 0
$$

This is the canonical **damped harmonic oscillator**. Divide by $I$ and define

$$
\omega_n = \sqrt{\frac{k}{I}} \quad \text{(undamped natural frequency, rad/s)}, \qquad
\zeta = \frac{c}{2\sqrt{kI}} \quad \text{(damping ratio, dimensionless)}
$$

to get the standard form:

$$
\ddot{\theta} + 2\zeta\omega_n\dot{\theta} + \omega_n^2\theta = 0
$$

## 2. The three regimes — and what they look like on a ragdoll

Solve with the ansatz $\theta = e^{\lambda t}$: $\;\lambda^2 + 2\zeta\omega_n\lambda + \omega_n^2 = 0$, so

$$
\lambda = -\zeta\omega_n \pm \omega_n\sqrt{\zeta^2 - 1}
$$

| Regime | Solution shape | On screen |
|---|---|---|
| **Underdamped** $\zeta < 1$ | $\theta(t) = A e^{-\zeta\omega_n t}\cos(\omega_d t + \varphi)$, with $\omega_d = \omega_n\sqrt{1-\zeta^2}$ | Limb overshoots the pose and **wobbles** — jelly arms, the classic bad-ragdoll look |
| **Critically damped** $\zeta = 1$ | $\theta(t) = (A + Bt)e^{-\omega_n t}$ | Fastest possible return with **zero overshoot** — usually the target for pose matching |
| **Overdamped** $\zeta > 1$ | $\theta(t) = Ae^{\lambda_1 t} + Be^{\lambda_2 t}$, both $\lambda$ real negative | Limb **oozes** back to pose — reads as moving through honey |

Useful numbers for the underdamped case:

- Overshoot fraction per half-cycle: $\;e^{-\pi\zeta/\sqrt{1-\zeta^2}}$. At $\zeta = 0.5$
  that's ~16% overshoot; at $\zeta = 0.2$, ~53% — very visible wobble.
- Settling time (to ~2% of the initial error): $\;t_s \approx \dfrac{4}{\zeta\omega_n}$.

**Design target for RagdollPro defaults:** slightly underdamped, $\zeta \approx 0.6\text{–}0.8$.
A hint of overshoot reads as organic; exact critical damping reads robotic. Check the defaults:
$k = 1000$, $c = 80$, and a mid-limb $I \approx 25$ (engine units) give

$$
\zeta = \frac{80}{2\sqrt{1000 \cdot 25}} = \frac{80}{316} \approx 0.25 \;\text{(per-joint, before multipliers)}
$$

— deliberately loose at the extremities; the `BoneProfiles` guidance (spine ×1.5–2 spring)
pushes the trunk toward $\zeta \sim 0.5$ while hands stay floppy.

## 3. Forced response: gravity droop and impact response

Add a constant external torque $\tau_g$ (gravity on the limb):

$$
I\ddot{\theta} + c\dot{\theta} + k\theta = \tau_g
\quad\Longrightarrow\quad
\theta_{ss} = \frac{\tau_g}{k}
$$

The steady-state droop depends **only on the spring**, not damping — damping shapes *how* you
get there, spring decides *where* you end up. This is why "the pose sags" is always a spring
problem and "the pose wobbles" is always a damping-ratio problem. Diagnose them separately.

For an impulsive load (a bullet delivering angular impulse $L$ to the child body), the initial
condition becomes $\dot{\theta}(0) = L/I$ and the peak deflection in the underdamped regime is

$$
\theta_{peak} \approx \frac{L}{I\,\omega_d}\, e^{-\zeta\omega_n t_{peak}}, \qquad t_{peak} = \frac{1}{\omega_d}\arctan\frac{\omega_d}{\zeta\omega_n}
$$

i.e. **stiffer joints (higher $\omega_n$) deflect less and recover faster** — which is exactly
why `HitReactionStrengthMultiplier > 1` makes a flinch snap back instead of dangle.

## 4. Why damping scales as √(spring scale) — the key derivation

RagdollPro composes one master scale $s \in [0, 1+]$ per tick (contact backoff, lifetime curve,
blend-out, gameplay). The naive implementation scales both drive terms linearly:

$$
k' = s\,k, \qquad c' = s\,c \qquad \text{(naive)}
$$

Compute the damping ratio under the naive scaling:

$$
\zeta' = \frac{c'}{2\sqrt{k'I}} = \frac{s\,c}{2\sqrt{s\,k\,I}} = \sqrt{s}\;\zeta
$$

So as contact backoff drops $s$ to $0.1$, the damping ratio collapses to
$\sqrt{0.1} \approx 0.32$ of its tuned value — a joint tuned at $\zeta = 0.7$ suddenly runs at
$\zeta = 0.22$. The body doesn't just go *soft* in the pileup, it goes **bouncy**, which is the
opposite of what a limp body should do.

Now scale damping by $\sqrt{s}$ instead:

$$
k' = s\,k, \qquad c' = \sqrt{s}\,c
\quad\Longrightarrow\quad
\zeta' = \frac{\sqrt{s}\,c}{2\sqrt{s\,k\,I}} = \frac{c}{2\sqrt{kI}} = \zeta
$$

$$
\boxed{\;c \propto \sqrt{k} \;\text{ keeps } \zeta \text{ invariant under any strength scale}\;}
$$

The oscillator gets *slower* ($\omega_n' = \sqrt{s}\,\omega_n$ — weaker muscle, longer time
constant) but keeps exactly the same **character** (same overshoot fraction, same decay shape).
Hence in `ApplyMotorStrength`:

```cpp
const float DampingScale = FMath::Sqrt(FMath::Max(Scale, 0.f));
// Spring  *= Scale
// Damping *= DampingScale
```

Bonus property: as $s \to 0$ the damping term dominates the spring term
($c'/k' = c/(k\sqrt{s}) \to \infty$). Residual damping acts as **pure joint friction** —
resistance to motion with no pose preference — which is precisely the physical behavior of an
unconscious body: floppy, but not frictionless. This surviving friction is what sells the
pileup case and what kills micro-wobble at rest.

## 5. Discrete-time stability (why the engine gets away with big springs)

An explicit (semi-implicit Euler) integration of the oscillator,

$$
\omega_{t+1} = \omega_t + \Delta t\,\frac{-k\theta_t - c\,\omega_t}{I}, \qquad
\theta_{t+1} = \theta_t + \Delta t\,\omega_{t+1}
$$

is only stable when $\Delta t < \dfrac{2}{\omega_n}$ (undamped case). At 60 FPS
($\Delta t = 1/60$) that caps $\omega_n < 120$ rad/s, i.e. $k < 120^2 I$. Beyond it, the
numerical solution gains energy every step and the ragdoll **explodes**.

Chaos solves drives **implicitly** (the drive is a row in the constraint solve, using
end-of-step velocities), which is unconditionally stable: energy is always removed, never
added, regardless of $k$ or $\Delta t$. Practical consequences:

- You may safely over-tune `BaseAngularSpring`; the failure mode is aesthetic (mannequin), not
  numerical (NaN).
- Stability does **not** mean accuracy: at low substep rates very stiff drives converge to a
  slightly wrong equilibrium and can shudder against contacts. If a stiff setup buzzes on
  contact, raise solver iterations/substeps before lowering the spring.

## 6. Tuning recipe (practical summary)

1. Set `MaxAngularForce = 0` while tuning shape, so saturation doesn't mask spring behavior.
2. Pick `BaseAngularSpring` from the **droop test**: hold an arm-out pose; adjust until sag
   looks like intended muscle tone ($\theta_{droop} = \tau_g/k$, doc 02 §6).
3. Pick `BaseAngularDamping` from the **flick test**: nudge a limb; adjust until it returns
   with one small overshoot (~$\zeta \approx 0.7$: $c = 1.4\sqrt{kI}$; with $I$ unknown, just
   scale $c$ by eye — the formula tells you it should move with the *square root* of any spring
   change you made in step 2).
4. Add `BoneProfiles`: spine/neck 1.5–2× spring, arms 0.5×, hands/feet 0.2×.
5. Only now set `MaxAngularForce` (30 000–100 000) until heavy impacts visibly win.
6. Contact backoff (`ContactDriveMultiplier`, default 0.1) needs no re-tuning after any of the
   above — §4 guarantees the character of the motion is preserved at every strength level.
