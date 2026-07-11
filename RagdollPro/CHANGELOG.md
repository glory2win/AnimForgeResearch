# Changelog

All notable changes to RagdollPro.

## [0.1.0] — 2026-07-11

First implementation. Not yet released on Fab.

### Added
- `URagdollProComponent` — constraint-native powered ragdoll controller, evolved from the
  AnimForge research module `RagdollMotorComponent`:
  - State machine `Inactive → BlendingIn → Active → Settling → BlendingOut → Done` with
    Blueprint delegates (`OnStateChanged`, `OnSettled`, `OnFinished`)
  - SLERP joint motors driven from the live animation pose, with freeze/unfreeze
  - Per-bone-chain strength profiles (exact-match / nearest-ancestor resolution)
  - Contact-aware strength backoff (decaying impulse accumulator)
  - Optional strength-over-lifetime curve
  - Rolling-window settle detection + hard stuck timeout
  - Partial-body hit reactions with auto-recovery
  - Impulse propagation up the parent chain
  - Owner-follow and getup helpers (`IsLyingFaceDown`, `GetPelvisWorldLocation`)
  - Constraint projection safety net, debug draw
- Plugin skeleton: `RagdollPro.uplugin`, runtime module, `LogRagdollPro` category
- Theory documentation (Docs 01–08): architecture, SLERP-drive PD control on SO(3),
  spring-damper theory incl. the √-damping law, contact backoff math, settle detection math,
  blending math, physics asset setup guide, roadmap

### Changed (vs. research module)
- Renamed `URagdollMotorComponent` → `URagdollProComponent`, `ERagdollMotorState` →
  `ERagdollProState`, `FRagdollMotorBoneProfile` → `FRagdollProBoneProfile`
- `YOURGAME_API` placeholder → `RAGDOLLPRO_API`
- `LogTemp` → `LogRagdollPro`
- Raw `UCurveFloat*` properties → `TObjectPtr<UCurveFloat>`
