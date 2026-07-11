# 08 — Roadmap: RagdollPro on Fab

AnimForge Studios' debut Fab plugin. This document is the working plan from the current v0.1
implementation to a paid Fab listing and beyond.

## Positioning

**"The ragdoll component AAA teams build in-house, as a drop-in plugin."**

Competing listings on Fab are mostly (a) thin wrappers around `PhysicalAnimationComponent`, or
(b) full active-ragdoll locomotion systems that are overkill for the 90% use case (deaths, hit
reactions, knockdowns). RagdollPro targets exactly that 90% with a constraint-native approach
none of the wrapper products use, and documents the math — the Docs folder ships with the
plugin and is itself a differentiator (tech-savvy buyers can audit every decision).

## Milestones

### v0.1 — First implementation ✅ (current)
- [x] `URagdollProComponent`: full state machine, SLERP joint motors, bone profiles,
      contact backoff, settle detection, hit reactions, owner follow, debug draw
- [x] Plugin packaging skeleton (`.uplugin`, module, Build.cs)
- [x] Theory documentation (docs 01–07)

### v0.2 — Validation pass
- [ ] Compile + test against UE 5.4, 5.5, 5.6 (Win64 first)
- [ ] Test project with UE5 Mannequin: death, hit-reaction, pileup, getup maps
      (the validation checklist from doc 07 §4 as reproducible levels)
- [ ] Automation tests: state-machine liveness (StuckTimeout guarantees), motor-table build
      against sparse physics assets, re-entrancy rules
- [ ] Profile: 20 simultaneous ragdolls; document ms/ragdoll on a mid-range target

### v0.3 — Feature completeness for launch
- [ ] **Getup flow helper**: pose snapshot capture built in (`OnSettled` → snapshot →
      exposed via anim curve/variable), facing-direction getup selection
- [ ] **Contact backoff hysteresis band** (separate on/off thresholds — doc 04 §6)
- [ ] **Per-profile damping-ratio mode**: author zeta directly instead of raw damping
      (doc 03 makes this a trivial transform; huge usability win)
- [ ] **Significance/LOD**: distant ragdolls tick motors and settle detection at reduced rate
- [ ] Blueprint sample content: pre-tuned BoneProfiles presets (Humanoid Default, Heavy,
      Zombie-limp), authored blend curves

### v1.0 — Fab launch
- [ ] Fab listing: demo video (pileup comparison vs PhysicalAnimationComponent side-by-side —
      the money shot), screenshots, feature matrix
- [ ] Documentation site or PDF from /Docs
- [ ] Pricing: single tier, personal == studio price at launch (lower friction for reviews)
- [ ] Support channel: dedicated email + Discord

### Post-1.0 candidates (demand-driven)
- **AnimGraph node version** (RigidBody-node-style, evaluate in the anim thread)
- **Network pose pull**: server-authoritative settle position, clients reconcile cosmetically
- **Active balance** (upright standing under small pushes) — entry into the active-ragdoll
  space if v1 sells; builds on the same motor table
- **Vehicle/mount dismount presets**
- **Mass-AI integration** (crowd deaths at scale with shared settle budget)

## Open questions

1. **Engine floor**: 5.4 (current `.uplugin`) or drop to 5.3 for larger install base?
   `FConstraintInstance` API used here is stable back to 5.0; cost is CI breadth.
2. **Name collision check** on Fab/Marketplace for "RagdollPro" before committing branding.
3. **Trademark/branding**: publish under "AnimForge Studios" (matches Fab publisher account).

## Working agreements

- Every runtime tunable added after v0.1 must land with its math documented in /Docs in the
  same commit — the docs-as-deliverable standard is part of the product.
- No feature enters v1.0 scope without a repro level in the test project.
