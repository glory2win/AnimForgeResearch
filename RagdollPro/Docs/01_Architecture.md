# 01 — Architecture

This document explains *how* RagdollPro is put together and *why* each structural decision was
made. The math behind individual subsystems lives in docs 02–06.

## 1. The core idea: power the constraints you already have

Every Physics Asset constraint (`FConstraintInstance`) ships with an angular drive — a motor
capable of applying torque between the two bodies it connects. In a normal ragdoll these drives
are off. `PhysicalAnimationComponent` ignores them and instead spawns an *extra* world-space
drive constraint per body.

RagdollPro flips this: it enables the **SLERP angular drive** on each existing constraint and
lets the engine stream drive *targets* from the animated local-space pose
(`USkeletalMeshComponent::bUpdateJointsFromAnimation`). Each joint then behaves like a muscle:

```
torque = Spring * angular_error(target_rotation, current_rotation)
       - Damping * relative_angular_velocity
```

with `target_rotation` expressed **relative to the parent bone**, exactly as an animator authored
it. See [02_JointDriveMath.md](02_JointDriveMath.md) for the full derivation.

Two architectural consequences fall out of this:

1. **World placement is never fought over.** The drives only care about the *shape* of the pose
   (joint angles). Where the body ends up in the world is decided entirely by gravity and
   contacts. This is why pileups look right: nothing pulls a forearm toward a world position
   that is now inside another corpse.
2. **Zero added simulation cost.** The constraints exist and are simulated anyway; enabling
   their drives costs a few solver iterations of extra work per joint, versus a whole extra
   constraint instance per body for Physical Animation.

## 2. Component anatomy

`URagdollProComponent` is a single `UActorComponent` with five subsystems:

```
                        ┌─────────────────────────────┐
                        │      URagdollProComponent    │
                        ├─────────────────────────────┤
 StartRagdoll() ───────>│  State machine              │──> OnStateChanged
 StartHitReaction() ───>│  (drives everything below)  │──> OnSettled / OnFinished
                        ├─────────────────────────────┤
                        │  Motor table                │──> SetAngularDriveParams()
                        │  (constraint* + multipliers)│    per FConstraintInstance
                        ├─────────────────────────────┤
 OnComponentHit ───────>│  Contact accumulator        │──> CurrentContactMultiplier
                        ├─────────────────────────────┤
                        │  Settle detector            │──> triggers BlendingOut
                        │  (velocity ring buffer)     │
                        ├─────────────────────────────┤
                        │  Blend controller           │──> SetAllBodiesBelowPhysicsBlendWeight()
                        └─────────────────────────────┘
```

### 2.1 The motor table

Built once per `Start*()` call, cleared on finish. One entry per driven constraint:

```cpp
struct FMotorEntry
{
    FConstraintInstance* Constraint; // non-owning; valid while physics state is alive
    float SpringMul;                 // resolved from BoneProfiles
    float DampingMul;
};
```

Design notes:

- **Raw pointer, rebuilt every start.** `FConstraintInstance` lifetimes are tied to the mesh's
  physics state. Rather than trying to track invalidation, the table is rebuilt on every
  `StartRagdoll`/`StartHitReaction` and dropped in `FinishRagdoll`. The component never touches
  a constraint outside the window where it also owns the simulation state.
- **Branch filtering at build time.** For partial simulations (hit reactions, `StartBone !=
  None`), only constraints whose *child* bone (`JointName`) is inside the simulated branch are
  driven. Driving a joint whose child body is kinematic would push torque into a body the
  solver treats as infinitely heavy.
- **Multipliers resolved once.** The `BoneProfiles` hierarchy walk (exact match beats nearest
  ancestor with `bIncludeChildren`) runs at table build time, not per tick.

### 2.2 Strength composition

Every tick, one scalar is composed and pushed to all motors:

```
Scale = GlobalMotorScale                  (gameplay: stun, death fade)
      × CurrentContactMultiplier          (contact backoff, doc 04)
      × StrengthOverLifetimeCurve(t)      (optional authored fade)
      × HitReactionStrengthMultiplier     (hit reactions only)
      × CurrentBlendWeight                (BlendingOut only, doc 06)
```

The per-joint values are then:

```
Spring  = BaseAngularSpring  × SpringMul(bone)  × Scale
Damping = BaseAngularDamping × DampingMul(bone) × sqrt(Scale)     <- doc 03 explains sqrt
```

A 2% change gate (`FMath::IsNearlyEqual(Scale, LastAppliedScale, 0.02f)`) skips the per-constraint
scene writes when nothing meaningful changed, so a settled ragdoll costs almost nothing.

## 3. The state machine

```
             StartRagdoll / StartHitReaction
                          │
   Inactive ──────────────┤
                          ▼
                     BlendingIn          physics blend weight 0 → 1 over BlendInDuration
                          │
                          ▼
                       Active            motors run; contact backoff; settle detection
                          │                (hit reactions: fixed timer instead)
              settle OR StuckTimeout
                          ▼
                      Settling           instantaneous marker state; OnSettled fires here
                          │
                          ▼
                     BlendingOut         blend weight 1 → 0; motors relax in lockstep
                          │
                          ▼
                        Done             OnFinished fires; component goes dormant
```

Why each state exists:

- **BlendingIn** — snapping blend weight to 1 in one frame pops visually; the physics pose and
  the animated pose always differ slightly at the moment of activation.
- **Active** — the only state with per-tick decision making (settle detection, owner follow).
- **Settling** — *deliberately zero-duration.* Its only job is to give listeners a clean,
  ordered marker between "was active" and "is blending out": the `OnSettled` broadcast happens
  between the two `SetState` calls, so a pose snapshot taken in that callback captures the body
  exactly at rest, before the blend-out starts moving bones back toward animation.
- **BlendingOut** — weight ramps down *and* motor strength is multiplied by the current blend
  weight, so the joints stop arguing with the animation that is taking over (doc 06).
- **Done vs Inactive** — `Done` records that a ragdoll ran to completion; both accept a new
  `Start*()`.

Re-entrancy rules:

- `StartRagdoll` is callable from **any** state — escalating a hit reaction into a full ragdoll
  simply re-initializes.
- `StartHitReaction` is only honored from `Inactive`/`Done` — a partial reaction never
  interrupts a full ragdoll that owns the body.
- `CancelRagdoll` snaps everything back instantly and does **not** fire `OnFinished` (callers
  distinguish "natural end" from "aborted").

## 4. Tick design

- **Tick group `TG_PostPhysics`.** All velocity reads (settle detection) and contact reactions
  happen *after* this frame's simulation. Ticking pre-physics would mean reacting to last
  frame's world one tick late.
- **Dormant by default.** The component disables its own tick in `BeginPlay` and only enables
  it between `Start*()` and finish. A hundred idle NPCs with RagdollPro components cost nothing.
- **Anim tick management.** Motor targets *are* the anim pose. If the mesh stops evaluating
  animation when off-screen (`VisibilityBasedAnimTickOption`), every off-screen ragdoll drives
  toward a stale pose. While active, the component forces
  `AlwaysTickPoseAndRefreshBones` and restores the previous setting on finish.

## 5. Event flow (typical death)

```
1. Damage system decides death
2. -> RagdollComp->StartRagdoll("pelvis")
3. -> (optional) RagdollComp->FreezeMotorTargets()   // strain toward the last living pose
4.    BlendingIn (0.35 s) -> Active
5.    body falls, collides; contact accumulator softens motors during the pileup
6.    velocities drop; rolling window says "settled" for 0.25 s
7. -> OnSettled fires -> game code snapshots pose (SnapshotPose / PoseAsset)
8.    BlendingOut (0.5 s): weight 1 -> 0 while AnimBP already plays the snapshot
9. -> OnFinished fires -> game code swaps to getup logic using IsLyingFaceDown()
```

## 6. What is deliberately NOT in v0.1

- **No networking.** Ragdolls are cosmetically simulated per-client; the authoritative position
  is the actor root (owner-follow keeps it meaningful). Deterministic replicated ragdolls are a
  non-goal; a "pose pull" reconciliation is on the roadmap (doc 08).
- **No LOD/significance manager.** Planned: distant ragdolls drop motor updates and settle
  detection to a low rate.
- **No animation-node integration.** The component drives the skeletal mesh directly; an
  AnimGraph node version (RigidBody-node-style) is a roadmap item.
