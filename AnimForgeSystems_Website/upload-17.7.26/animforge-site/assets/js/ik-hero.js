/* ============================================================
   AnimForge Systems — hero IK demo
   A genuine 2D damped least-squares (Levenberg–Marquardt style)
   IK solver running live on the hero canvas.

   Solve per frame:  dTheta = J^T (J J^T + lambda^2 I)^-1 * e
   J is 2×N (planar), so (J J^T + lambda^2 I) is a 2×2 inverse — cheap.
   ============================================================ */

(function () {
  "use strict";

  const canvas = document.getElementById("ik-canvas");
  if (!canvas) return;

  const ctx = canvas.getContext("2d");
  const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  // ---------- chain setup ----------
  const NUM_BONES = 6;
  const LAMBDA = 0.35;            // damping factor (shown in the hero caption)
  const ITERATIONS = 8;           // solver iterations per frame
  const theta = new Array(NUM_BONES).fill(0).map((_, i) => (i === 0 ? -0.4 : 0.35));
  let boneLen = 70;               // recomputed on resize
  let rootX = 0, rootY = 0;

  // trail of end-effector positions (fading trajectory, UE debug-draw style)
  const TRAIL_MAX = 90;
  const trail = [];

  // target: cursor when inside hero, otherwise a slow Lissajous idle path.
  // The solver never sees the raw target — it sees a smoothed one (see below),
  // so pointer enter/leave and fast cursor moves can't snap the chain.
  let mouseX = null, mouseY = null;
  let smoothX = null, smoothY = null;   // low-pass filtered target
  const SMOOTHING = 0.12;               // per-frame lerp factor toward raw target
  const MAX_DTHETA = 0.09;              // per-joint, per-frame angular velocity clamp (rad)
  let t = 0;

  const css = getComputedStyle(document.documentElement);
  const COL = {
    grid:   css.getPropertyValue("--grid").trim()   || "#2A323C",
    spline: css.getPropertyValue("--spline").trim() || "#5FD4E8",
    ember:  css.getPropertyValue("--ember").trim()  || "#F2762E",
    muted:  css.getPropertyValue("--muted").trim()  || "#8B98A5",
  };

  // ---------- resize ----------
  function resize() {
    const hero = canvas.parentElement;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = hero.clientWidth * dpr;
    canvas.height = hero.clientHeight * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    const w = hero.clientWidth, h = hero.clientHeight;
    rootX = w * (w < 720 ? 0.5 : 0.62);
    rootY = h * 0.86;
    boneLen = Math.min(w, h) * 0.115;
  }

  // ---------- forward kinematics ----------
  // Returns joint positions [ [x,y], ... ] with NUM_BONES+1 entries.
  function fk(angles) {
    const pts = [[rootX, rootY]];
    let a = 0, x = rootX, y = rootY;
    for (let i = 0; i < NUM_BONES; i++) {
      a += angles[i];
      x += Math.cos(a) * boneLen;
      y += Math.sin(a) * boneLen;
      pts.push([x, y]);
    }
    return pts;
  }

  // ---------- one DLS iteration ----------
  function dlsStep(tx, ty) {
    const pts = fk(theta);
    const [ex, ey] = pts[NUM_BONES];
    let dx = tx - ex, dy = ty - ey;

    // clamp error step so the chain moves smoothly rather than snapping
    const errLen = Math.hypot(dx, dy);
    const maxStep = boneLen * 0.8;
    if (errLen > maxStep) { dx = (dx / errLen) * maxStep; dy = (dy / errLen) * maxStep; }

    // Jacobian columns: J_i = cross(z, p_e - p_i)  →  (-(ey-piy), (ex-pix)) in 2D
    const jx = new Array(NUM_BONES), jy = new Array(NUM_BONES);
    for (let i = 0; i < NUM_BONES; i++) {
      jx[i] = -(ey - pts[i][1]);
      jy[i] =  (ex - pts[i][0]);
    }

    // A = J J^T + lambda^2 I  (2×2)
    const l2 = (LAMBDA * boneLen) * (LAMBDA * boneLen);
    let a00 = l2, a01 = 0, a11 = l2;
    for (let i = 0; i < NUM_BONES; i++) {
      a00 += jx[i] * jx[i];
      a01 += jx[i] * jy[i];
      a11 += jy[i] * jy[i];
    }

    // solve A * f = e  (2×2 inverse)
    const det = a00 * a11 - a01 * a01;
    if (Math.abs(det) < 1e-8) return;
    const fx = ( a11 * dx - a01 * dy) / det;
    const fy = (-a01 * dx + a00 * dy) / det;

    // dTheta = J^T f
    for (let i = 0; i < NUM_BONES; i++) {
      theta[i] += jx[i] * fx + jy[i] * fy;
    }
  }

  // ---------- drawing ----------
  function drawGrid(w, h) {
    ctx.strokeStyle = COL.grid;
    ctx.globalAlpha = 0.45;
    ctx.lineWidth = 1;
    const step = 40;
    ctx.beginPath();
    for (let x = 0.5; x < w; x += step) { ctx.moveTo(x, 0); ctx.lineTo(x, h); }
    for (let y = 0.5; y < h; y += step) { ctx.moveTo(0, y); ctx.lineTo(w, y); }
    ctx.stroke();
    ctx.globalAlpha = 1;
  }

  function drawChain(pts, tx, ty) {
    // trail
    ctx.lineWidth = 1.5;
    for (let i = 1; i < trail.length; i++) {
      const alpha = (i / trail.length) * 0.5;
      ctx.strokeStyle = COL.spline;
      ctx.globalAlpha = alpha;
      ctx.beginPath();
      ctx.moveTo(trail[i - 1][0], trail[i - 1][1]);
      ctx.lineTo(trail[i][0], trail[i][1]);
      ctx.stroke();
    }
    ctx.globalAlpha = 1;

    // bones
    ctx.strokeStyle = COL.spline;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(pts[0][0], pts[0][1]);
    for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i][0], pts[i][1]);
    ctx.stroke();

    // joints
    for (let i = 0; i < pts.length; i++) {
      const isEnd = i === pts.length - 1;
      ctx.beginPath();
      ctx.arc(pts[i][0], pts[i][1], isEnd ? 5 : 3.5, 0, Math.PI * 2);
      ctx.fillStyle = isEnd ? COL.ember : "#14181D";
      ctx.strokeStyle = isEnd ? COL.ember : COL.spline;
      ctx.lineWidth = 1.5;
      ctx.fill();
      ctx.stroke();
    }

    // target gizmo (cross + circle)
    ctx.strokeStyle = COL.ember;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.arc(tx, ty, 9, 0, Math.PI * 2);
    ctx.moveTo(tx - 14, ty); ctx.lineTo(tx - 4, ty);
    ctx.moveTo(tx + 4, ty);  ctx.lineTo(tx + 14, ty);
    ctx.moveTo(tx, ty - 14); ctx.lineTo(tx, ty - 4);
    ctx.moveTo(tx, ty + 4);  ctx.lineTo(tx, ty + 14);
    ctx.stroke();
  }

  function rawTargetPos() {
    if (mouseX !== null) return [mouseX, mouseY];
    // idle Lissajous path around the chain's workspace
    const cx = rootX, cy = rootY - boneLen * 2.6;
    return [
      cx + Math.sin(t * 0.55) * boneLen * 2.4,
      cy + Math.sin(t * 0.83 + 1.2) * boneLen * 1.5,
    ];
  }

  // Clamp the target into the chain's comfortable workspace:
  // an annulus [minReach, maxReach] around the root. Targets near the root
  // force folded near-singular configurations; targets past full extension
  // make the solve oscillate against the reach boundary.
  function clampToWorkspace(x, y) {
    const minReach = boneLen * 1.6;
    const maxReach = boneLen * (NUM_BONES - 0.35);
    let dx = x - rootX, dy = y - rootY;
    let d = Math.hypot(dx, dy);
    if (d < 1e-6) { dx = 0; dy = -1; d = 1; }
    const r = Math.min(Math.max(d, minReach), maxReach);
    return [rootX + (dx / d) * r, rootY + (dy / d) * r];
  }

  function targetPos() {
    const [rx, ry] = clampToWorkspace(...rawTargetPos());
    if (smoothX === null) { smoothX = rx; smoothY = ry; }
    smoothX += (rx - smoothX) * SMOOTHING;
    smoothY += (ry - smoothY) * SMOOTHING;
    return [smoothX, smoothY];
  }

  function frame() {
    t += 0.016;
    const w = canvas.parentElement.clientWidth;
    const h = canvas.parentElement.clientHeight;
    const [tx, ty] = targetPos();

    // solve, then clamp each joint's total per-frame delta (angular velocity limit)
    const prev = theta.slice();
    for (let i = 0; i < ITERATIONS; i++) dlsStep(tx, ty);
    for (let i = 0; i < NUM_BONES; i++) {
      const d = theta[i] - prev[i];
      if (d >  MAX_DTHETA) theta[i] = prev[i] + MAX_DTHETA;
      if (d < -MAX_DTHETA) theta[i] = prev[i] - MAX_DTHETA;
    }

    const pts = fk(theta);
    trail.push(pts[NUM_BONES]);
    if (trail.length > TRAIL_MAX) trail.shift();

    ctx.clearRect(0, 0, w, h);
    drawGrid(w, h);
    drawChain(pts, tx, ty);

    requestAnimationFrame(frame);
  }

  // ---------- input ----------
  const hero = canvas.parentElement;
  hero.addEventListener("pointermove", (e) => {
    const r = hero.getBoundingClientRect();
    mouseX = e.clientX - r.left;
    mouseY = e.clientY - r.top;
  });
  hero.addEventListener("pointerleave", () => { mouseX = null; mouseY = null; });

  window.addEventListener("resize", resize);
  resize();

  if (reduceMotion) {
    // static solved pose, no animation loop
    const [tx, ty] = [rootX + boneLen * 2.0, rootY - boneLen * 2.6];
    for (let i = 0; i < 60; i++) dlsStep(tx, ty);
    const w = hero.clientWidth, h = hero.clientHeight;
    ctx.clearRect(0, 0, w, h);
    drawGrid(w, h);
    drawChain(fk(theta), tx, ty);
  } else {
    requestAnimationFrame(frame);
  }
})();
