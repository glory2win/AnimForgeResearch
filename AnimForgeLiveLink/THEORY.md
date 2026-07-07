# THEORY — Root Motion Warping from First Principles

This note derives the math in `Shared/AnimForgeWarpVizShared/SkewWarpMath.h`
(mirrored by `Scripts/warpviz_skewwarp.py`). Notation: column vectors,
right-handed, all quantities in the space of the warp-window origin.

## 1. Problem statement

Over a **warp window** `[t₀, t₁]` the source animation moves the root along a
trajectory `p(t)`, with

- origin `p₀ = p(t₀)`,
- source displacement `D = p(t₁) − p₀`,
- required displacement `T = target − p₀`.

We want a warped trajectory `p′(t)` such that

1. `p′(t₀) = p₀` (no pop at the window start),
2. `p′(t₁) = p₀ + T` (the root lands exactly on the warp target),
3. the *shape* of the motion — foot weaving, hip sway, the acceleration
   profile — is preserved as much as possible,
4. the map is linear in the local displacement, so it is cheap, stable, and
   composes with per-frame evaluation.

## 2. The minimal-change linear map is a shear

Look for a linear map `W` applied to local displacements,
`p′(t) = p₀ + W · (p(t) − p₀)`, subject to the endpoint constraint `W·D = T`.

Among all linear maps satisfying `W·D = T`, take the one closest to identity —
"change nothing except what the constraint forces". Minimize `‖W − I‖²_F`
(Frobenius norm) subject to `W·D = T`. With a Lagrange vector `λ`:

```
L(W, λ) = ‖W − I‖²_F − λᵀ(W·D − T)
∂L/∂W = 2(W − I) − λ Dᵀ = 0   ⟹   W = I + ½ λ Dᵀ
```

Substituting into the constraint: `D + ½ λ (DᵀD) = T`, so `½ λ = (T − D)/(D·D)`:

```
W = I + (T − D) Dᵀ / (D·D)
```

A rank-one update of identity — a **shear (skew)**, hence "SkewWarp". Two
properties fall straight out:

- **Endpoint exactness**: `W·D = D + (T − D)(D·D)/(D·D) = T`. ∎
- **Shape preservation**: for any `v ⊥ D`, `Dᵀv = 0`, so `W·v = v`. Every
  component of the motion perpendicular to the net displacement — the weave,
  the sway — passes through *unchanged*. Only the component along `D` is
  redirected/rescaled toward the target. This is precisely requirement 3, and
  it is the reason warped locomotion still "reads" like the source clip.

When `T = D` (target where the animation already lands), `W = I` identically:
the warp is a no-op, as it must be.

### Method variants

- **SkewWarp** — the shear above, plus rotation correction (§4).
- **SimpleWarp** — same shear, translation only (rotation untouched).
- **Scale** — projects the constraint onto the source direction:
  `s = (T·D)/(D·D)`, `p′ = p₀ + s·(p − p₀)`. Stretches/compresses the stride
  but never corrects lateral error; useful to check "is my target simply too
  far?" in isolation.

### Degenerate case: in-place animations

If `‖D‖ ≈ 0` the shear is undefined (nothing to redirect). We fall back to
ramping the offset in directly:

```
p′(t) = p(t) + T · α(t)
```

with `α(t)` the progress function of §4. Endpoint exactness still holds
(`α(t₁) = 1`), start is unchanged (`α(t₀) = 0`).

## 3. Outside the window

- **Before** `t₀`: samples pass through untouched.
- **After** `t₁`: the remaining motion is carried **rigidly**. With
  `Δp = T − D` the translation offset and `q_c` the rotation correction at the
  window end (§4), each later sample is rotated about the window-end pivot and
  offset:

```
p′(t) = p′(t₁) + q_c · (p(t) − p(t₁))
q′(t) = q_c · q(t)
```

This keeps `p′` C⁰-continuous at `t₁` and preserves every post-window
per-frame delta (up to the rotation), so follow-through motion is intact.

## 4. Rotation correction keyed to arc length

The orientation error at the window end is `q_c = q_target · q(t₁)⁻¹`.
Applying it all at once would snap; applying it on a fixed clock turns the
character while standing still during motion holds. Instead we key the blend
to **arc-length progress**:

```
α(t) = (∫ₜ₀ᵗ ‖dp‖) / (∫ₜ₀ᵗ¹ ‖dp‖)          (falls back to normalized time
q′(t) = slerp(1, q_c, α(t)) · q(t)           when the path length is ~0)
```

The character turns *while it travels* — matching how the runtime modifier
distributes rotation across the window, and how animators expect a turn to
read. `α` is monotonic, so the correction never oscillates
(`test_rotation_correction_is_progressive` pins this down).

## 5. Correspondence with UE's Motion Warping

UE's `URootMotionModifier_SkewWarp` warps each frame's *remaining* root-motion
delta so the accumulated motion lands on the sync-point — an online, incremental
formulation, because at runtime the future can change (the target may move).
Our gym evaluates a *known* clip over a *fixed* window offline, so the closed
form above applies; for a static target both formulations redirect the same
displacement onto the same endpoint with lateral shape preserved. The gym's
answer is what the runtime modifier converges to — which is exactly what the
animator needs to see. (If frame-perfect parity against a specific engine
version is ever required, swap `FWarpVizEvaluator::ApplyWarp` to call the
engine modifier directly; it is the single seam where the math is applied.)

## 6. Invariants pinned by tests

| Invariant | Test |
|---|---|
| `W·D = T` exactly | `SkewMatrixTests.test_matrix_maps_original_delta_to_target_delta` / C++ `TestSkewWarpMatrix` |
| `W·v = v` for `v ⊥ D` | `test_perpendicular_components_preserved` |
| `T = D ⟹ W = I` | `test_identity_when_target_equals_original` |
| Endpoint hits target (trajectory level) | `test_endpoint_hits_target_exactly`, UE `SkewWarp.EndpointHitsTarget` |
| Window start unmoved | `test_start_of_window_unchanged` |
| Pre-window untouched / post-window rigid | `test_samples_before_window_untouched`, `test_post_window_motion_carried_rigidly` |
| Lateral shape preserved | `test_shape_preserved_lateral_weave` |
| In-place fallback ramps monotonically to target | `test_degenerate_in_place_anim_ramps_offset` |
| Rotation reaches target, monotonically | `test_rotation_correction_reaches_target/_is_progressive` |

The same invariants are asserted in Python (`Tests/Python/test_skewwarp.py`),
standalone C++ (`Tests/Cpp/TestMain.cpp`) and UE Automation
(`SkewWarpMathTests.cpp`) — the three implementations cannot drift apart
without a red test.
