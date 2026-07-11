# 06 — Blending Math: Physics Blend Weight and Motor Relaxation Coupling

Getting into and out of a ragdoll without a visible pop is a blending problem. This doc covers
what the engine's physics blend weight actually does, how RagdollPro shapes it with curves, and
the one subtle coupling that makes blend-out clean: relaxing the motors in lockstep with the
blend weight.

## 1. What `PhysicsBlendWeight` actually is

For every bone with a physics body, the engine computes two candidate transforms each frame:

- $T_{anim}$ — the animated (kinematic) transform from the AnimBP output;
- $T_{phys}$ — the simulated body transform from the physics scene.

The rendered bone transform is the blend, with weight $w \in [0,1]$
(`SetAllBodiesPhysicsBlendWeight` / `...Below...`):

$$
T_{final} = \mathrm{blend}(T_{anim},\, T_{phys},\, w) =
\begin{cases}
\text{position: } & (1-w)\,\mathbf{x}_{anim} + w\,\mathbf{x}_{phys}\\[2pt]
\text{rotation: } & \mathrm{slerp}(q_{anim},\, q_{phys},\, w)
\end{cases}
$$

Key mental model: **the simulation always runs at full strength; only the *rendered* skeleton
is blended.** At $w = 0.5$ the physics bodies are exactly where physics put them — the mesh
just draws halfway between. Consequences:

- Blending is purely cosmetic and therefore cheap and always stable — no feedback into the sim
  from the blend itself (with one caveat in §4).
- At $w = 1$, moving the actor/component transform doesn't move the rendered bones (they follow
  world-space physics bodies). This is what makes owner-follow invisible while fully ragdolled.
- Branch-scoped variants (`SetAllBodiesBelowPhysicsBlendWeight`) let a hit reaction blend only
  the affected limb while the rest of the body stays 100% animated.

## 2. Blend-in: shaping $w(t)$

During `BlendingIn`, normalized time $\alpha = t / D_{in}$ maps to weight through an optional
authored curve:

$$
w(t) = \begin{cases} C_{in}(\alpha) & \text{curve set (full ragdolls only)}\\ \alpha & \text{linear fallback} \end{cases}
$$

Why shape it at all: at the moment of death the physics pose and animated pose differ (the
capsule was driving locomotion; the body has just been given an impulse). A linear ramp spreads
the correction evenly; an ease-in curve ($C_{in}$ starting shallow) hides the initial
divergence in the frames where the animation is still dominant, then commits quickly.
Recommended authoring: smoothstep-like, $C_{in}(\alpha) = 3\alpha^2 - 2\alpha^3$.

Hit reactions ignore the curve and use a very short linear ramp
(`HitReactionBlendInDuration = 0.08` s) — a flinch must register within ~2 frames of the shot
or it reads as delayed, and at that duration curve shape is imperceptible.

## 3. Blend-out: why the curve is applied directly, descending

During `BlendingOut` the weight must go 1 → 0. RagdollPro evaluates the authored curve
**directly as the weight** (not as an eased alpha):

$$
w(t) = \begin{cases} C_{out}(\alpha) & \text{curve set, authored } 1 \to 0\\ 1 - \alpha & \text{linear fallback} \end{cases}
$$

Direct evaluation keeps authoring WYSIWYG — the curve you see in the editor *is* the weight
over normalized time, no mental inversion required. The cost is the convention that the curve
must be authored descending; the header documents it.

At this point the AnimBP is already playing the settled-pose snapshot (captured in `OnSettled`,
doc 01 §5, which fired *before* blend-out began), so the blend is between "physics at rest" and
"animation of that same rest pose" — nearly zero divergence, which is why even a fast 0.5 s
blend-out reads clean.

## 4. The coupling: motor strength × blend weight

The one non-cosmetic interaction. During `BlendingOut`, `ComposeGlobalScale()` multiplies motor
strength by the current blend weight:

$$
s_{blendout}(t) = s_{base} \cdot w(t)
$$

Why this matters: the *simulation* doesn't know about the render-side blend. Without this
coupling, at $w = 0.2$ the viewer sees 80% animation, but the physics bodies underneath are
still being driven at full muscle strength toward the (frozen or live) motor targets — targets
that may disagree with the snapshot animation now playing. The bodies fight, the remaining 20%
physics contribution jitters, and the handover shivers.

Scaling motor strength by $w$ means: as the animation takes over the *picture*, the motors
surrender the *simulation* at exactly the same rate. At the end, `StopAllMotors()` zeroes the
drives and simulation stops entirely. And because damping follows the $\sqrt{s}$ law
(doc 03 §4), the joints lose strength during this ramp without ever changing damping character
— the fade-out is invisible.

The same term is exactly why `CancelRagdoll()` (which skips `BlendingOut`) can afford to snap:
it's for teleports/respawns where visual continuity is already forfeit.

## 5. Frame-rate behavior

Both ramps advance by accumulated real time ($\alpha = t_{state}/D$), not per-frame increments,
so total blend duration is exact regardless of frame rate; a hitch mid-blend jumps $w$ forward
but never overshoots (clamped $\alpha \le 1$). The only frame-rate-sensitive element is curve
*sampling* granularity, which is visually irrelevant at any playable frame rate.

## 6. Authoring guidance

| Asset | Shape | Duration |
|---|---|---|
| `BlendInCurve` (death) | smoothstep ease-in-out | 0.3–0.4 s |
| `BlendOutCurve` (getup/snapshot) | descending, gentle start then commit ($1 - $ smoothstep) | 0.4–0.6 s |
| Hit reaction in | (no curve — linear) | 0.05–0.1 s |
| Hit reaction out | (no curve — linear) | 0.3–0.4 s |

If you see:

- **Pop at ragdoll start** → lengthen `BlendInDuration` or ease the curve start.
- **Shiver during blend-out** → confirm your snapshot is actually playing (the blend target
  must be the settled pose, not a locomotion pose); the motor coupling handles the rest.
- **Corpse "wakes up" briefly at blend-out start** → your `BlendOutCurve` starts below 1.0;
  the first frame of `BlendingOut` then *drops* physics contribution discontinuously. The curve
  must start at exactly $C_{out}(0) = 1$.
