## Home
### Title: AnimForge Systems — Real-Time Animation Systems R&D

## Hero || Animation Systems R&D
============ HERO — live DLS IK solver (C)2026 IK solvers, text and tech stack discussions here are copy righted to AnimForge Systems. Unauthorized copying is strictly prohibited. ===========

#### Section: >Animation systems R&amp;D
<p>
          IK solvers, ragdoll stability, N&#8209;dimensional blend spaces, and Maya&#8596;Unreal
          pipeline tooling — built in C++ against real engine internals, shipped as
          drop&#8209;in components.
</p>
<p class="hero-caption">live: damped least-squares IK &middot; 6 bones &middot; &lambda; = 0.35 &middot; move your cursor</p>



  <!-- ============  PHILOSOPHY AND POSITIONING ==============-->

### Philosophy
#### The systems beneath the motion
AnimForge Systems is an independent animation systems R&D studio focused on real-time animation, engine technology, and DCC tooling.

We build production-oriented tools for Unreal, Maya, and other real-time environments while maintaining our own research platforms for exploring algorithms and animation architectures across different runtime and authoring environments.

The goal is not to replace existing engines. It is to understand animation systems deeply enough to extend them, integrate with them, and build better tools around them.The systems beneath the motion
AnimForge Systems is an independent animation systems R&D studio focused on real-time animation, engine technology, and DCC tooling.

We build production-oriented tools for Unreal, Maya, and other real-time environments while maintaining our own research platforms for exploring algorithms and animation architectures across different runtime and authoring environments.

The goal is not to replace existing engines. It is to understand animation systems deeply enough to extend them, integrate with them, and build better tools around them.

### Tools

#### Extending the animation stack
Every tool targets a specific problem in real-time animation — extending existing engine capabilities where needed, exploring new approaches where useful, and shipping with the engineering notes behind the implementation.


#### DLS IK Solver Node
A damped least-squares IK AnimNode using axis-angle Jacobian error computation. Designed for improved stability near singularities and configurable per-chain damping.

#### RBF Blend Space
N-dimensional blend spaces via radial basis function interpolation — a more general alternative to traditional 2D blend-space workflows. Engine-portable core solver.

#### Ragdoll Stability Stack
Drop-in components for ragdoll lifecycle: state-machine driven blend in/out, impulse-based contact detection, rolling-velocity settle detection, and per-joint drives.

#### AnimForge WarpViz
Bidirectional Motion Warping visualization between Maya and Unreal. Warp targets are evaluated natively in-engine and returned to Maya as a transparent animation layer overlay.


### The Lab
#### Prani — an animation engine built to answer questions
Prani is a lightweight, multithreaded, ECS-based animation engine built from scratch — a laboratory for asking difficult questions about animation systems. How should poses be represented? How should solvers scale? Where should work happen? Ideas are explored, measured, and validated here. Validated ideas can then be adapted into engine plugins, production tools, or further research.

### Writing

#### Engineering notes, not marketing copy
Deep dives into engine internals, solver math, and the architecture decisions behind each tool.

#### PAC vs. PhysicsBlendWeight: two knobs that are not the same knob


### CONTRACTS
#### Need an animation systems engineer?
Available for contract work on runtime animation systems, physics animation, and DCC pipeline tooling — UE5 C++, Maya API, C#, Python.

-------------------------------------------------

## Tools Page

### Tools

#### The AnimForge toolkit
Runtime animation components for Unreal Engine and pipeline tooling for Maya. Each entry below links its demo video and the engineering notes behind it.


### 01 . Shipping

#### DLS IK Solver Node
A damped least-squares inverse kinematics AnimNode. Where CCD twists and FABRIK drifts near singular configurations, DLS regularizes the solve — the damping term trades a small amount of tracking accuracy for unconditional stability at full extension and near joint limits.

The error term is computed in axis-angle space via the rotation log-map, the same formulation the engine's Physical Animation Component uses internally for its orientation drives — which makes solver behavior predictable when the two interact.

[ demo video — YouTube embed goes here ]

Solver — damped least-squares with per-chain λ control
Error term — axis-angle (log-map) Jacobian error computation
Stability — well-behaved near singularities and full extension
Integration — standard AnimGraph node, LOD-aware evaluation
Platform — Unreal Engine 5, C++

### 02 · In development

#### RBF Blend Space
Unreal's blend space assets stop at two dimensions — the Delaunay triangulation and barycentric weighting they're built on doesn't generalize past 2D. Real gameplay parameters don't stop at two: aim yaw, aim pitch, lean, speed, stance.

The AnimForge RBF Blend Space node interpolates sample poses in N dimensions using radial basis functions with cardinal interpolation, clamp-and-renormalize negative weight handling, and top-k pruning to bound runtime cost. The core solver is a standalone, engine-independent library — built for portability to Unity and custom engines from day one.

[ 2D weight-field visualizer clip goes here ]
Dimensions — N-dimensional input, past the stock 2D ceiling
Kernels — Gaussian and polyharmonic, per-asset selectable
Weights — cardinal interpolation with clamp-and-renormalize
Runtime cost — top-k sample pruning with bounded evaluation
Portability — standalone C++ solver, engine adapters on top

### 03 · In development

#### Ragdoll Stability Stack
Getting into ragdoll is easy. Getting out — cleanly, on time, without jitter — is where projects burn weeks. The stability stack is a set of layered, drop-in ActorComponents that own the full ragdoll lifecycle.

RagdollStabilityComponent drives a BlendingIn → Active → Settling → BlendingOut → Done state machine with impulse-accumulator contact detection and rolling-velocity settle detection — because native physics sleep is not a reliable settle trigger. A companion joint-drive layer adds per-joint orientation targets with independent swing/twist gains.

[ ragdoll settle & recover demo goes here ]
Lifecycle — explicit state machine, Blueprint-assignable events
Settle detection — ring-buffer velocity window + hard timeout
Contacts — impulse-accumulator based, filters resting chatter
Joint drives — per-joint targets, separate swing/twist gains
Integration — pose snapshot & blend logic built in, Chaos physics

### 04 · R&D

#### AnimForge WarpViz
Motion Warping decisions get made in Unreal, but animation gets authored in Maya — and animators can't see what warping will do to their work until it's in engine. WarpViz closes that loop.

On request, a pre-configured Unreal evaluation level natively evaluates the warp targets and returns the warped root trajectory and pose trail back into Maya as a transparent animation layer overlay — the animator sees engine-accurate warping directly in their authoring viewport. Snapshot request-response, not fragile per-frame streaming.

[ Maya ↔ Unreal warp overlay demo goes here ]
Direction — bidirectional Maya ↔ Unreal bridge
Evaluation — native in-engine warp evaluation, exact parity
Overlay — warped trajectory + pose trail as a Maya anim layer
Architecture — on-demand snapshot evaluation, no streaming loop
V1 scope — root motion trajectory visualization

### Want release updates?
Tools land on the marketplace as they ship. Follow along, or get in touch if you want early access for your team.


----------------------
## Lab Page

### The lab

#### Prani
A lightweight, multithreaded, ECS-based animation engine built from scratch — AnimForge's research sandbox for modern animation techniques.

#### Why build an engine to build tools?
Iterating on animation algorithms inside a full game engine means paying the full engine's cost every cycle: long rebuilds, heavyweight editors, and abstractions you have to work around instead of with. Prani exists to make the iteration loop on solver math, blending strategies, and pose pipelines as short as possible.

Algorithms are proven here first — with visualizers and unit tests — then ported into engine plugins. The DLS IK solver and the RBF blend space core both follow this path: validate in Prani, ship in Unreal.

Language — modern C++, built from scratch
ECS — EnTT registry, command-buffer structural changes
Threading — Intel TBB task parallelism across pose evaluation
Rendering — Raylib 6 with native skeletal animation blending
Assets — glTF import via cgltf
Tooling UI — Dear ImGui, composited into desktop tooling

### Current research threads

### What's on the bench
<em>Active</em>
~~~
Parallel pose pipeline
ECS pose-view architecture with TBB parallel_for across skeleton evaluation — measuring where task granularity pays off and where it doesn't.

Active
IK iteration environment
The DLS solver math running in a bare Prani node — sub-second rebuild cycles for solver experiments that would take minutes per iteration in a full engine.

Exploring
Tooling composition
Raylib rendering composited into a desktop tooling UI — an FBO render-to-texture bridge, with shared GL context as the long-term target.

Exploring
WarpViz evaluation target
Prani as a lightweight replacement for a full Unreal instance in the WarpViz pipeline — making the tool dramatically easier to distribute.
~~~

### DEVLOG
#### Build notes
Architecture decisions, dead ends, and measurements from Prani's development — posted as they happen.

#### Devlog #1 coming soon — why Prani exists

--------------------------------------------------

## Writing Page

### Engineering notes
Engine internals, solver math, and the architecture decisions behind AnimForge tools — written for animation programmers and technical animators.

#### PAC vs. PhysicsBlendWeight: two knobs that are not the same knob

-----------------------------------------------------

### About

### One engineer, all layers of the animation stack
AnimForge Systems is Varma Dandu — a senior animation engineer working across runtime systems, physics animation, and DCC pipeline tooling.

#### Background
I work at the intersection where animation, physics, and engine internals meet: AnimGraph nodes and solver math in Unreal Engine C++, constraint-level ragdoll work against Chaos physics, and Maya API pipeline tooling that connects DCC authoring to runtime behavior.

AnimForge exists because the gaps I kept engineering around in production — 2D-capped blend spaces, unreliable ragdoll settle detection, warping you can't preview in Maya — are gaps every animation team hits. The tools on this site are those solutions, productized.

Everything here follows the same method: validate the algorithm standalone with tests and visualizers, then integrate. The Prani lab engine is that method turned into infrastructure.

Runtime — Unreal Engine 5 C++, AnimGraph, Chaos, Motion Warping
Physics animation — PAC, constraint instances, ragdoll lifecycle
DCC pipeline — Maya API (C++ / Python), PySide2 tooling
Languages — C++, C#, Python
Also — Unity, custom engines, build systems (UBT)

### Contracts

#### Work with me
Available for contract engagements: runtime animation features, physics animation systems, IK and procedural animation, animation pipeline and DCC tooling. Remote-friendly, based in the Helsinki area (EET).

contact@animforgesystems.com
LinkedIn — Varma Dandu
GitHub — glory2win

-------------------------------------------
Hire Me button links to Contracts->Work with me