# 07 — Physics Asset Setup Guide

RagdollPro powers the constraints that already exist in your Physics Asset. That means the
asset's quality is the ceiling on the ragdoll's quality. This is the checklist we validate
against before blaming the component.

## 1. Hard requirements

### 1.1 Angular limits must NOT be Locked on driven joints

**The single most common setup failure.** SLERP angular drives are silently ignored by the
engine on constraints whose angular swing/twist DOFs are all `Locked`. Symptoms: the ragdoll is
pure floppy physics, motor scale shows correctly in debug draw, nothing holds pose.

For every constraint you want driven, in the Physics Asset Editor:

- **Angular Limits → Swing 1 / Swing 2 / Twist Motion**: set `Limited` (preferred) or `Free`.
  Never all three `Locked`.
- Set the limit **angles** to anatomical ranges — the drive pulls toward the pose, the limits
  are the hard stop that prevents impossible poses under big impacts:

| Joint | Twist | Swing 1 | Swing 2 |
|---|---|---|---|
| Spine segments | ±10–15° | 20–30° | 15–25° |
| Neck/head | ±30° | 40° | 30° |
| Shoulder (clavicle→upperarm) | ±40° | 90–110° | 80° |
| Elbow | ±15° | 100–130° (one-sided feel via ref frame) | 5–10° |
| Hip | ±30° | 70–90° | 50° |
| Knee | ±10° | 100–130° | 5–10° |
| Ankle/wrist | ±25° | 30–45° | 20–30° |

(Exact values are skeleton- and rig-convention-dependent; treat these as starting points.)

### 1.2 Bodies: coverage and mass

- **One body per major bone**, none on twist/roll/IK/attach helper bones. Typical humanoid:
  11–15 bodies. Constraints only form between consecutive *bodies*, so a helper bone with a
  body in the middle of the forearm creates a joint that shouldn't exist.
- **Sane masses.** Chaos computes mass from shape volume × density by default; verify the total
  (`Physics Asset Editor → Tools → body masses`) lands near 60–90 kg for a humanoid and that no
  extremity out-masses the trunk. Wildly wrong mass ratios (>10:1 between constrained
  neighbors) are the #1 cause of joint jitter under load, because the solver converges slowly
  on heavy-light pairs. The pelvis/spine should be the heaviest bodies.
- **Collision shapes**: capsules for limbs, box or convex for pelvis/chest. Avoid tiny shapes
  (< ~4 cm across) — thin shapes tunnel and jitter.

### 1.3 Collision filtering inside the asset

- Disable collision between every constrained (parent↔child) body pair — the Physics Asset
  Editor does this by default when creating constraints, but verify after editing shapes.
- *Enable* collision between non-adjacent pairs that realistically touch (hand↔pelvis,
  forearm↔chest); this is what prevents arms sinking into the torso in a pile.

## 2. Strongly recommended

### 2.1 Projection on pileup-prone constraints

Constraint projection positionally snaps a drifted joint back within tolerance after the solve,
instead of correcting through velocity (which takes frames and looks rubbery). Enable it
(Physics Asset Editor, per constraint) on shoulders, hips, and neck — the joints that get
levered hardest in pileups.

RagdollPro forces `bEnableProjection = true` with full linear/angular alpha at runtime as a
safety net (`bAutoEnableConstraintProjection`), but authoring it per constraint with tuned
tolerances is strictly better: blanket full-alpha projection can eat legitimate joint
compliance in exchange for stability.

### 2.2 Solver settings

For 5+ simultaneous ragdolls interacting:

- **Project Settings → Physics → Solver Iterations**: position 8+, velocity 2+ for visibly
  stacked bodies (default 6/1 is tuned for props).
- Consider enabling **async physics** with a fixed tick if the game's frame rate is unstable —
  drive behavior (docs 02–03) is frame-rate independent, but contact resolution quality isn't.

### 2.3 Damping on bodies

Small linear damping (0.1–0.3) and angular damping (0.5–1.0) on all bodies helps bodies settle
and compensates for the energy that blended kinematic bones can inject (see §3). This is
per-body damping in the asset, independent of the joint drive damping RagdollPro controls.

## 3. Known interactions

- **Partial simulation (hit reactions).** Simulated branch bodies hang from a kinematic parent
  chain. The kinematic side is infinitely heavy from the solver's perspective — a violently
  animated parent (sprint cycle) whips the simulated branch. If flinches look overdriven during
  fast locomotion, lower the impulse rather than the motor strength.
- **Root motion + owner follow.** `bFollowPelvisWithOwner` moves the actor root every tick
  while ragdolled. Disable your movement component's root-motion consumption during ragdoll or
  the two will fight over the root transform.
- **Scaled meshes.** Non-uniform actor scale breaks constraint frames in Chaos. Keep ragdolled
  characters at uniform scale, ideally 1.0.

## 4. Validation checklist (run before every release)

1. `StartRagdoll("pelvis")` on flat ground → body holds recognizable pose shape while falling,
   settles < 3 s, `OnSettled` fires, blend-out clean.
2. Same, with `bDrawDebug = true` → motor scale reads 1.0 in flight, drops toward
   `ContactDriveMultiplier` on landing, recovers.
3. Drop 5 ragdolls onto each other → no buzzing, no interpenetration-launches, all five reach
   `Done` ≤ `StuckTimeout` + blend time.
4. `StartHitReaction` on each limb at idle and at sprint → branch flinches and recovers; rest
   of body unaffected.
5. 300 damage-events/minute soak → no drift in `GetCurrentMotorScale`, no constraint warnings
   in `LogRagdollPro`.
6. Off-screen death (camera turned away) → body still settles correctly
   (`bManageAnimTickOption` verification).
