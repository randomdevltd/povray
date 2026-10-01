import { test } from 'node:test';
import assert from 'node:assert/strict';
import { dist, fn, matrix, ops, rotate, scale, shape, translate, fold, unitQuad } from '../src/index.ts';
import type { Op, Vec3 } from '../src/index.ts';

const { sin, cos, PI } = Math;
const base = fold({ radius: 0.4 })(unitQuad);
const grid = Array.from({ length: 49 }, (_, k): [number, number] => [(k % 7) / 6, Math.floor(k / 7) / 6]);

function same(a: Op, b: Op, tol = 1e-12) {
  const sa = a(base), sb = b(base);
  return Math.max(...grid.map(([u, v]) => dist(sa(u, v), sb(u, v)))) < tol;
}

test('scale about an origin leaves that origin fixed', () => {
  const o: Vec3 = [0.3, -0.2, 0.5];
  const s = scale([2, 0.5, 3], o)(() => o);
  assert.ok(dist(s(0, 0), o) < 1e-15);
  assert.ok(dist(scale(({ v }) => 1 + v, o)(() => o)(0.4, 0.7), o) < 1e-15);
});

test('rotate about an origin leaves that origin fixed', () => {
  const o: Vec3 = [1, 2, 3];
  assert.ok(dist(rotate([1, 1, 0], 1.3, o)(() => o)(0, 0), o) < 1e-15);
  assert.ok(dist(rotate([0, 0, 1], ({ u }) => 5 * u, o)(() => o)(0.9, 0), o) < 1e-15);
});

test('matrix identity is a no-op, 3x3 and 4x4', () => {
  assert.ok(same(matrix([[1, 0, 0], [0, 1, 0], [0, 0, 1]]), ops()));
  assert.ok(same(matrix([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]), ops()));
});

test('translate then translate back is the identity', () => {
  assert.ok(same(ops(translate([0.3, -1, 2]), translate([-0.3, 1, -2])), ops(), 1e-15));
  assert.ok(same(ops(translate(({ v }) => [v, 0, -v]), translate(({ v }) => [-v, 0, v])), ops(), 1e-15));
});

test('scale, rotate, translate and matrix each match the equivalent fn', () => {
  assert.ok(same(scale([1, 2, 3]), fn(({ x, y, z }) => [x, 2 * y, 3 * z])));
  assert.ok(same(scale(2, [1, 0, 0]), fn(({ x, y, z }) => [1 + 2 * (x - 1), 2 * y, 2 * z])));
  assert.ok(same(scale(({ v }) => 1 + v), fn(({ x, y, z, v }) => [(1 + v) * x, (1 + v) * y, (1 + v) * z])));
  assert.ok(same(translate([1, 2, 3]), fn(({ x, y, z }) => [x + 1, y + 2, z + 3])));
  assert.ok(same(rotate([0, 0, 1], PI / 2), fn(({ x, y, z }) => [-y, x, z])));
  assert.ok(same(rotate([0, 1, 0], ({ y }) => y), fn(({ x, y, z }) => [x * cos(y) + z * sin(y), y, -x * sin(y) + z * cos(y)])));
  assert.ok(same(matrix([[0, -1, 0, 2], [1, 0, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]), fn(({ x, y, z }) => [2 - y, x, z])));
});

test('scaling y by 2 before a fold doubles the height of the result', () => {
  const s = shape({}, scale([1, 2, 1]), fold({ radius: 1 })).surface;
  assert.ok(Math.abs(s(0.3, 1)[1] - 2) < 1e-15);
});
