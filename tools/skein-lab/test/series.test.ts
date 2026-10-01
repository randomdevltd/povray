import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildMesh, by, dist, fold, path, scale, shape, spline, volume } from '../src/index.ts';
import type { Interp, Vec3 } from '../src/index.ts';

const pts: Vec3[] = [[0, 0, 0], [1, 0.5, 0], [1.5, 2, 1], [0.2, 2.5, -0.5], [-1, 1, 0]];
const kinds: Interp[] = ['linear', 'catmull', 'cubic'];

test('path passes through its points at uniform parameters, open and closed', () => {
  for (const interp of kinds)
    for (const closed of [false, true]) {
      const p = path(...pts, { interp, closed });
      const n = closed ? pts.length : pts.length - 1;
      pts.forEach((q, i) => assert.ok(dist(p(i / n), q) < 1e-12, `${interp} closed=${closed} point ${i}`));
    }
});

test('spline passes through its knots at their parameter values', () => {
  const s = spline([0, [0, 0, 0]], [0.1, [1, 0, 0]], [0.6, [1, 1, 0]], [1, [0, 2, 1]]);
  for (const [t, q] of [[0, [0, 0, 0]], [0.1, [1, 0, 0]], [0.6, [1, 1, 0]], [1, [0, 2, 1]]] as [number, Vec3][])
    assert.ok(dist(s(t), q) < 1e-12);
});

test('closed loops are continuous across t = 1 for every interpolation', () => {
  for (const interp of kinds) {
    const p = path(...pts, { interp, closed: true });
    assert.ok(dist(p(1), p(0)) < 1e-12);
    assert.ok(dist(p(1 - 1e-7), p(1e-7)) < 1e-5, interp);
    const t0 = dist(p(1e-4), p(0)) / 1e-4, t1 = dist(p(1), p(1 - 1e-4)) / 1e-4;
    if (interp !== 'linear') assert.ok(Math.abs(t0 - t1) / t0 < 1e-2, `${interp} speed jump ${t0} vs ${t1}`);
  }
});

const arc = (f: (t: number) => Vec3, a: number, b: number, n = 400) => {
  let len = 0;
  for (let k = 0; k < n; k++) len += dist(f(a + ((b - a) * (k + 1)) / n), f(a + ((b - a) * k) / n));
  return len;
};

test('arclength option spaces samples evenly (measured along the curve, not by chord)', () => {
  const knots: Vec3[] = [[0, 0, 0], [0.1, 0, 0], [3, 0, 0], [3, 3, 0]];
  const p = path(...knots, { interp: 'catmull', arclength: true });
  const steps = Array.from({ length: 50 }, (_, i) => arc(p, i / 50, (i + 1) / 50));
  const spread = (Math.max(...steps) - Math.min(...steps)) / (steps.reduce((a, b) => a + b) / 50);
  const raw = path(...knots, { interp: 'catmull' });
  const rawSteps = Array.from({ length: 50 }, (_, i) => arc(raw, i / 50, (i + 1) / 50));
  console.log(`arclength: step spread ${(100 * spread).toFixed(3)}% of the mean (uniform parameter: max/min ${(Math.max(...rawSteps) / Math.min(...rawSteps)).toFixed(1)})`);
  assert.ok(spread < 0.01);
  assert.ok(Math.max(...rawSteps) / Math.min(...rawSteps) > 5);
});

test('a scalar spline works as a taper: linear knots give the frustum volume', () => {
  const taper = path(1, 0.25);
  const sh = shape({ wrap: 'u', ends: 'flat' }, scale([1, 2, 1]), fold({ radius: taper }));
  const v = volume(buildMesh(sh, undefined, undefined, false)) ?? NaN;
  const want = (Math.PI * 2 * (1 + 0.25 + 0.0625)) / 3;
  console.log(`scalar-spline taper: volume ${v.toFixed(6)} want ${want.toFixed(6)}`);
  assert.ok(Math.abs(v - want) / want < 0.005);
});

test('by() drives a series from another input; direct use is driven by v', () => {
  const ramp = path(0, 1);
  const direct = shape({}, fold({ radius: ramp })).surface;
  const byU = shape({}, fold({ radius: by(ramp, 'u') })).surface;
  const byExpr = shape({}, fold({ radius: by(ramp, ({ u, v }) => u * v) })).surface;
  assert.ok(Math.abs(Math.hypot(direct(0.3, 0.7)[0], direct(0.3, 0.7)[2]) - 0.7) < 1e-12);
  assert.ok(Math.abs(Math.hypot(byU(0.3, 0.7)[0], byU(0.3, 0.7)[2]) - 0.3) < 1e-12);
  assert.ok(Math.abs(Math.hypot(byExpr(0.3, 0.7)[0], byExpr(0.3, 0.7)[2]) - 0.21) < 1e-12);
});
