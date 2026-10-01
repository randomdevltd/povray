import { test } from 'node:test';
import assert from 'node:assert/strict';
import { dist, fold, shape } from '../src/index.ts';
import type { Surface, Vec3 } from '../src/index.ts';

const ring = (s: Surface, v: number, n: number): Vec3[] => Array.from({ length: n }, (_, i) => s(i / n, v));
const nearest = (p: Vec3, set: Vec3[]) => Math.min(...set.map((q) => dist(p, q)));

test('a twisted circle fold is the untwisted point set, ring by ring', () => {
  const plain = shape({}, fold({ radius: 1 })).surface;
  const twisted = shape({}, fold({ radius: ({ v }) => [1, 5 * v, 0] })).surface;
  let worst = 0;
  for (let j = 0; j <= 20; j++) {
    const v = j / 20;
    const coarseA = ring(plain, v, 64), coarseB = ring(twisted, v, 64);
    const denseA = ring(plain, v, 8192), denseB = ring(twisted, v, 8192);
    for (const p of coarseB) worst = Math.max(worst, nearest(p, denseA));
    for (const p of coarseA) worst = Math.max(worst, nearest(p, denseB));
  }
  console.log(`twist invariance: worst ring-point distance ${worst.toExponential(2)} (dense spacing ${(2 * Math.PI / 8192).toExponential(2)})`);
  assert.ok(worst < Math.PI / 8192 + 1e-12);
});

test('twist does move points: the parameterisation differs', () => {
  const plain = shape({}, fold({ radius: 1 })).surface;
  const twisted = shape({}, fold({ radius: ({ v }) => [1, 5 * v, 0] })).surface;
  assert.ok(dist(plain(0.1, 0.5), twisted(0.1, 0.5)) > 0.5);
});

test('radius as [r, angle, axial], as { radius, angle, axial }, and as named twist/axial options agree', () => {
  const a = shape({}, fold({ radius: ({ v }) => [0.7, 2 * v, 0.1 * v] })).surface;
  const b = shape({}, fold({ radius: ({ v }) => ({ radius: 0.7, angle: 2 * v, axial: 0.1 * v }) })).surface;
  const c = shape({}, fold({ radius: 0.7, twist: ({ v }) => 2 * v, axial: ({ v }) => 0.1 * v })).surface;
  for (const [u, v] of [[0.1, 0.2], [0.8, 0.9]]) {
    assert.ok(dist(a(u, v), b(u, v)) < 1e-12);
    assert.ok(dist(a(u, v), c(u, v)) < 1e-12);
  }
});
