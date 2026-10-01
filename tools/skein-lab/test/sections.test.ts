import { test } from 'node:test';
import assert from 'node:assert/strict';
import { dist, fold, ngon, shape, superellipse } from '../src/index.ts';

const { PI, cos, sin } = Math;

test('n-gon radius is 1 at corners and cos(pi/n) mid-edge', () => {
  const r = ngon(6);
  const at = (theta: number) => r({ theta });
  assert.ok(Math.abs(at(0) - 1) < 1e-12 && Math.abs(at(PI / 3) - 1) < 1e-12);
  assert.ok(Math.abs(at(PI / 6) - cos(PI / 6)) < 1e-12);
});

test('superellipse with p = 2 is the circle', () => {
  const r = superellipse(2);
  for (const theta of [0, 0.4, 2, 5]) assert.ok(Math.abs(r({ theta }) - 1) < 1e-12);
});

test('a curve-mode circle with an offset sheet matches the polar fold', () => {
  const thick = (u: number, v: number) => [u, v, 0.2] as [number, number, number];
  const polar = fold({ radius: 1 })(thick);
  const curve = fold({ curve: (t) => [cos(2 * PI * t), sin(2 * PI * t)] })(thick);
  for (const [u, v] of [[0.1, 0.3], [0.6, 0.8]]) assert.ok(dist(polar(u, v), curve(u, v)) < 1e-6);
});
