# RagdollPro

**Constraint-native powered ragdolls for Unreal Engine.**
AnimForge Studios — debut plugin for [Fab](https://fab.com).

RagdollPro replaces the `PhysicalAnimationComponent` pipeline with joint motors that live
*inside* the Physics Asset constraints. Every joint applies torque toward its animated
**parent-relative** rotation — muscles across a joint, not strings from the sky. The result is a
ragdoll that holds the *shape* of the pose while collisions, pileups, and impacts still read as
real physics.

## Why constraint-native beats Physical Animation

| Problem with `PhysicalAnimationComponent` | RagdollPro answer |
|---|---|
| World-space drive targets fight collision response (bodies dragged toward positions inside other bodies) | Parent-relative SLERP drive targets — pose *shape* is held, world placement is left to the solver |
| One extra drive constraint instance per body | Zero extra constraints; powers the drives already in the Physics Asset |
| Unclamped corrective forces — the pose always "wins" | `MaxAngularForce` torque budget per joint; heavy impacts visibly overpower the pose |
| Blind curve-driven strength through pileups | Contact-aware backoff from a decaying impulse accumulator |
| Blend-out waits on physics sleep (which chatters forever on messy contacts) | Rolling-window velocity settle detection + hard stuck timeout |

## Feature set (v0.1)

- **One component, no dependencies** — drop `URagdollProComponent` next to any
  `SkeletalMeshComponent`. No `PhysicalAnimationComponent`, no extra constraints.
- **Full state machine** — `Inactive → BlendingIn → Active → Settling → BlendingOut → Done`,
  with `BlueprintAssignable` delegates at every transition.
- **Live pose matching** — motor targets streamed from the animation pose every frame;
  `FreezeMotorTargets()` locks the death pose at any instant.
- **Per-bone-chain strength profiles** — strong spine, weak limbs, floppy hands, resolved by
  hierarchy walk with exact-match and nearest-ancestor rules.
- **Contact-aware strength backoff** — decaying impulse accumulator softens the body the moment
  contact gets messy (stacks, pileups, props).
- **Strength-over-lifetime curve** — author "life leaving the body" as a single float curve.
- **Partial-body hit reactions** — `StartHitReaction()` simulates one branch with boosted
  motors, applies the impulse, auto-recovers on a timer.
- **Impulse propagation** — `ApplyImpulseToBone()` walks the parent chain with geometric
  falloff so a shot to the hand also moves the arm.
- **Owner follow + getup helpers** — keep the actor root over the pelvis; `IsLyingFaceDown()`
  picks the getup animation.
- **Robust settle detection** — normalized rolling-window velocity test with sustain hysteresis
  and a hard timeout; never waits on the engine's sleep flag.

## Repository layout

```
RagdollPro/
├── RagdollPro.uplugin
├── README.md                  <- you are here
├── CHANGELOG.md
├── Docs/                      <- the theory. Every tunable is derived, not guessed.
│   ├── 01_Architecture.md         Component design & state machine
│   ├── 02_JointDriveMath.md       SLERP drives as PD control on SO(3)
│   ├── 03_SpringDamperTheory.md   Harmonic oscillator, damping ratio, sqrt-damping law
│   ├── 04_ContactBackoffMath.md   Leaky integrator / decaying impulse accumulator
│   ├── 05_SettleDetectionMath.md  Rolling-window filter, hysteresis, timeout
│   ├── 06_BlendingMath.md         Physics blend weight & motor relaxation coupling
│   ├── 07_PhysicsAssetSetup.md    Required Physics Asset configuration
│   └── 08_Roadmap.md              Fab release plan & planned features
└── Source/
    └── RagdollPro/
        ├── RagdollPro.Build.cs
        ├── Public/
        │   ├── RagdollPro.h               Module + log category
        │   └── RagdollProComponent.h      The component
        └── Private/
            ├── RagdollPro.cpp
            └── RagdollProComponent.cpp
```

## Quick start

1. Copy the `RagdollPro/` folder into your project's `Plugins/` directory and enable it.
2. Add a **RagdollPro Component** to any actor with a `SkeletalMeshComponent`.
3. In the Physics Asset, make sure angular swing/twist on driven joints is **Limited**
   (not Locked) — see [Docs/07_PhysicsAssetSetup.md](Docs/07_PhysicsAssetSetup.md).
4. Call `StartRagdoll("pelvis")` from your death AnimNotify, or
   `StartHitReaction(HitBone, Impulse)` from your damage handler.
5. Bind `OnSettled` for your pose snapshot and `OnFinished` for cleanup.

```cpp
// Death
RagdollComp->StartRagdoll(TEXT("pelvis"));
RagdollComp->FreezeMotorTargets(); // strain toward the final pose

// Flinch without dying
RagdollComp->StartHitReaction(Hit.BoneName, ShotDirection * 3000.f);
```

## Requirements

- Unreal Engine 5.4+ (Chaos physics)
- A Physics Asset with sensible constraint limits (setup guide in Docs)

## Status

`v0.1.0` — first implementation, pre-Fab-release. See [CHANGELOG.md](CHANGELOG.md) and
[Docs/08_Roadmap.md](Docs/08_Roadmap.md).

---
© AnimForge Studios. All rights reserved. Distributed under the Fab EULA once published;
not open source.
