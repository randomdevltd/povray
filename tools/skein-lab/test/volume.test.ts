import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildMesh, volume, pathLength } from '../src/index.ts';
import torus from '../examples/torus.ts';
import frustum from '../examples/frustum.ts';
import knot, { trefoil } from '../examples/knot.ts';

const relErr = (got: number, want: number) => Math.abs(got - want) / want;

function check(name: string, sh: Parameters<typeof buildMesh>[0], want: number, tol: number) {
  const v = volume(buildMesh(sh, undefined, undefined, false));
  assert.ok(v !== null, `${name} mesh not closed`);
  console.log(`${name}: volume ${v.toFixed(6)} want ${want.toFixed(6)} err ${(100 * relErr(v, want)).toFixed(3)}%`);
  assert.ok(relErr(v, want) < tol, `${name} volume ${v} vs ${want}`);
}

test('torus volume is 2 pi^2 R r^2 within 0.5%', () => check('torus', torus, 2 * Math.PI ** 2 * 2 * 0.25, 0.005));

test('frustum volume is pi h (R^2 + R r + r^2) / 3 within 0.5%', () =>
  check('frustum', frustum, (Math.PI * 2 * (1 + 0.25 + 0.0625)) / 3, 0.005));

test('knot tube volume is pi r^2 L within 1%', () => check('knot', knot, Math.PI * 0.05 ** 2 * pathLength(trefoil), 0.01));
