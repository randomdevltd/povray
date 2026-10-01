import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildMesh, dot, norm, spherical, shape } from '../src/index.ts';
import type { Vec3 } from '../src/index.ts';
import sphere from '../examples/sphere.ts';

function radiusAndNormals(sh: typeof sphere) {
  const m = buildMesh(sh);
  let radiusErr = 0, worstDot = 1;
  for (let k = 0; k < m.positions.length / 3; k++) {
    const p: Vec3 = [m.positions[3 * k], m.positions[3 * k + 1], m.positions[3 * k + 2]];
    const n: Vec3 = [m.normals[3 * k], m.normals[3 * k + 1], m.normals[3 * k + 2]];
    radiusErr = Math.max(radiusErr, Math.abs(norm(p) - 1));
    worstDot = Math.min(worstDot, dot(n, p) / norm(p));
  }
  return { radiusErr, worstDot };
}

test('raw-fn sphere: unit radius and outward finite-difference normals, poles included', () => {
  const { radiusErr, worstDot } = radiusAndNormals(sphere);
  console.log(`sphere: max radius error ${radiusErr.toExponential(2)}, worst normal dot ${worstDot.toFixed(7)}`);
  assert.ok(radiusErr < 1e-12);
  assert.ok(worstDot > 0.9999);
});

test('spherical() helper matches the raw sphere', () => {
  const a = shape({}, spherical()).surface, b = sphere.surface;
  for (const [u, v] of [[0.1, 0.2], [0.7, 0.9], [0.33, 0.5]]) assert.ok(norm(sub3(a(u, v), b(u, v))) < 1e-12);
});

const sub3 = (a: Vec3, b: Vec3): Vec3 => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
