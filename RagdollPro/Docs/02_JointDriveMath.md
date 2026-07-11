# 02 — Joint Drive Math: SLERP Drives as PD Control on SO(3)

This is the heart of RagdollPro. It derives, from first principles, what
`SetAngularDriveParams(Spring, Damping, MaxForce)` actually computes, why the SLERP drive mode
is used instead of twist/swing, and what the numbers you type into `BaseAngularSpring` /
`BaseAngularDamping` physically mean.

## 1. The control problem

Each Physics Asset constraint connects a **parent body** $P$ and a **child body** $C$. Let

- $q_P, q_C \in \mathbb{H}_1$ — world-space orientations (unit quaternions) of the two bodies,
- $q_{rel} = q_P^{-1} \otimes q_C$ — the **current** orientation of the child relative to the parent,
- $q_{tgt}$ — the **target** relative orientation, sampled from the animated local-space pose
  (this is what `bUpdateJointsFromAnimation` refreshes every frame),
- $\omega_{rel} = \omega_C - \omega_P$ — relative angular velocity (world space, rad/s).

The drive must produce a torque $\tau$ on the child (and $-\tau$ on the parent, by Newton's
third law) that rotates $q_{rel}$ toward $q_{tgt}$ without oscillating forever. This is a
**setpoint regulation problem on the rotation group SO(3)** — the rotational analogue of a
spring-damper pulling a point toward a target position.

## 2. Rotation error: the axis-angle of the difference quaternion

Rotations don't subtract like vectors, so the "error" must be defined multiplicatively. The
quaternion that carries the current relative orientation onto the target is

$$
q_{err} = q_{tgt} \otimes q_{rel}^{-1}
$$

Every unit quaternion encodes an axis $\hat{n}$ and angle $\theta$:

$$
q_{err} = \left(\cos\tfrac{\theta}{2},\; \hat{n}\sin\tfrac{\theta}{2}\right),
\qquad \theta \in [0, \pi]
$$

(The solver picks the sign of $q_{err}$ so that $\theta \le \pi$ — the *short way around*.
Without this "quaternion double-cover" fix, a target 350° away would be chased 350° instead of
−10°.)

The **rotation error vector** is the axis-angle (logarithm map of SO(3)):

$$
\mathbf{e} = \theta\,\hat{n} \;=\; 2\,\mathrm{atan2}\!\left(\lVert q_{err,xyz}\rVert,\; q_{err,w}\right)\cdot \frac{q_{err,xyz}}{\lVert q_{err,xyz}\rVert}
$$

For small errors this reduces to $\mathbf{e} \approx 2\,q_{err,xyz}$, which is the cheap
approximation many engines use inside the solver.

## 3. The PD control law

The drive torque is a **proportional-derivative (PD) controller** on that error:

$$
\boxed{\;\tau \;=\; k_s\,\mathbf{e} \;-\; k_d\,\omega_{rel}\;}
$$

where, in Unreal terms:

| Symbol | Unreal parameter | Units (conceptually) |
|---|---|---|
| $k_s$ | `Spring` (our `BaseAngularSpring × SpringMul × Scale`) | torque per radian of error |
| $k_d$ | `Damping` (our `BaseAngularDamping × DampingMul × √Scale`) | torque per rad/s of velocity |
| $\lVert\tau\rVert_{max}$ | `MaxForce` (our `MaxAngularForce`) | torque saturation limit |

Interpretation of each term:

- **Proportional term $k_s\,\mathbf{e}$** — a torsional spring. Torque grows linearly with how
  far the joint is from its animated angle, along the single axis that most directly corrects
  the error. This is what *holds the pose*.
- **Derivative term $-k_d\,\omega_{rel}$** — a torsional damper. Note it damps toward **zero
  relative velocity**, not toward the target's velocity. Two consequences:
  1. It kills oscillation (the spring alone would ring forever — see doc 03).
  2. It acts as **joint friction** even when the spring is weak. This is why RagdollPro keeps
     damping comparatively high during contact backoff: pure friction with no pose-holding is
     exactly how a real unconscious body behaves in a pile.
- **Saturation $\lVert\tau\rVert \le \tau_{max}$** — a muscle can only pull so hard. With
  `MaxAngularForce = 0` (unclamped) the pose *always* wins any argument with an impact; with a
  finite budget, a large impulse produces error faster than the clamped torque can correct it,
  and the limb visibly gives way. That reads as weight.

### 3.1 Implicit integration (why huge springs don't explode)

Chaos (like PhysX before it) solves drives **implicitly**: the torque applied during the step
uses the *end-of-step* velocity, i.e. it solves

$$
\tau = k_s\,\mathbf{e}(t+\Delta t) - k_d\,\omega_{rel}(t+\Delta t)
$$

as part of the constraint solve rather than evaluating the right-hand side at the start of the
step. Explicit spring integration is only stable when $\Delta t \lesssim 2/\sqrt{k_s/I}$
(see doc 03 §5); implicit integration is unconditionally stable, which is why you can type
`BaseAngularSpring = 10000` at 30 FPS and get a stiff joint instead of NaNs. The practical
consequence: **spring values are safe to over-tune; the failure mode is a rigid mannequin look,
not an explosion.**

## 4. SLERP drive vs twist/swing drive

Unreal offers two angular drive modes; RagdollPro forces SLERP. The difference matters.

### Twist/swing decomposition

The relative rotation is split around the joint's twist axis $\hat{t}$ (usually the bone
direction):

$$
q_{rel} = q_{swing} \otimes q_{twist}, \qquad
q_{twist} = \text{projection of } q_{rel} \text{ onto } \hat{t}
$$

Twist and swing then get **independent** springs. Problems for full-body pose matching:

1. **Coupled errors decouple wrongly.** A real pose error is one rotation; splitting it means
   the two springs correct along axes that don't compose back into the shortest path. Under
   load the joint visibly "corkscrews" — swing corrects first, twist lags, the bone spirals in.
2. **Gimbal-adjacent singularities.** The swing decomposition degenerates as swing approaches
   180°; near the singularity tiny orientation changes produce huge decomposed-angle changes,
   which the springs amplify into jitter.
3. **Per-axis tuning burden.** Every joint needs twist and swing tuned separately.

### SLERP drive

One spring acts on the full 3-DOF error $\mathbf{e} = \theta\hat{n}$ directly — the torque
always points along the **geodesic** (the SLERP path, hence the name) from current to target
orientation. Shortest path, no decomposition singularities, one number to tune. For humanoid
pose matching, where errors are usually mixtures of twist and swing, this is uniformly more
stable, which is why `BuildMotorTable()` sets:

```cpp
Constraint->SetAngularDriveMode(EAngularDriveMode::SLERP);
Constraint->SetOrientationDriveSLERP(true);
Constraint->SetAngularVelocityDriveSLERP(true);
```

**Caveat (engine behavior):** SLERP drives are silently ignored on constraints whose angular
DOFs are all `Locked`. This is the #1 setup mistake — see doc 07.

## 5. Why parent-relative targets are the whole point

`PhysicalAnimationComponent` (world-space mode) drives each body toward its animated **world**
transform:

$$
\tau_{PA} = k_s\,\mathbf{e}\!\left(q^{world}_{tgt},\, q^{world}_C\right) + \text{linear drive toward } \mathbf{x}^{world}_{tgt}
$$

Consider a ragdoll landing on a stack of bodies. The animated world pose has the pelvis at
standing height and the hands at its sides — positions now occupied by other corpses. The
world-space drive pulls **into** the penetration; the contact solver pushes **out** of it. The
two run at cross purposes every substep and the visible result is buzzing, sliding, and the
"possessed corpse" look.

The parent-relative drive has no opinion about world placement:

$$
\tau_{RP} = k_s\,\mathbf{e}\!\left(q^{parent\text{-}rel}_{tgt},\, q_P^{-1} \otimes q_C\right) - k_d\,\omega_{rel}
$$

If the whole body is shoved somewhere else, every $q_P^{-1} \otimes q_C$ is unchanged — zero
corrective torque. The pose *shape* is held; the pose *placement* is surrendered to physics.
That single property eliminates the entire class of pileup fighting bugs, and it's why
RagdollPro doesn't need most of the babysitting the old `RagdollStabilityComponent` did.

## 6. What the tuning numbers mean physically

For a child body with moment of inertia $I$ about the correction axis (a UE mannequin forearm
is on the order of $I \sim 10\text{–}50\ \mathrm{kg\,cm^2}$ in engine units, mass in kg and
length in cm):

- **Undamped natural frequency** of the joint spring:
  $\;\omega_n = \sqrt{k_s / I}\;$ rad/s. With $k_s = 1000$ and $I = 25$, $\omega_n \approx 6.3$
  rad/s ≈ 1 Hz — the limb rings back to pose in roughly a second. That's the "start ~500–2000"
  guidance on `BaseAngularSpring`.
- **Damping ratio** $\;\zeta = \dfrac{k_d}{2\sqrt{k_s I}}\;$ — the dimensionless number that
  decides ringy vs sluggish. Doc 03 is entirely about this.
- **Static hold**: gravity applies torque $\tau_g \approx m g\, \ell_{cm} \sin\alpha$ about a
  joint holding a limb at angle $\alpha$ from vertical ($\ell_{cm}$ = distance to the limb's
  center of mass). The steady-state pose error (droop) is
  $\;\theta_{droop} = \tau_g / k_s.$
  A 4 kg forearm+hand at $\ell_{cm}=15$ cm held horizontally needs
  $\tau_g \approx 4 \cdot 980 \cdot 15 \approx 6\times10^4$ in engine units; at
  $k_s = 1000$ that droops a full radian (soup), at $k_s = 10^5$ it droops ~0.6° (rigid). Real
  tuning lives between — and this computation is why per-bone `SpringMultiplier`s exist:
  the spine carries more gravity torque than a finger.

## 7. Summary of the torque pipeline in RagdollPro

Per driven joint, per solver iteration:

```
e      = axis_angle( shortest( q_target_from_anim ⊗ q_rel⁻¹ ) )       // rad, vector
tau    = (S_base · S_bone · Scale) · e  -  (D_base · D_bone · √Scale) · w_rel
tau    = clamp_norm(tau, MaxAngularForce)                              // muscle budget
apply +tau to child body, -tau to parent body                          // implicit solve
```

with `Scale` composed per tick from gameplay, contact, lifetime, and blend factors
(doc 01 §2.2), and `√Scale` on damping keeping the damping ratio constant (doc 03 §4).
