import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildMesh, volume, pathLength } from '../src/index.ts';
import torus from '../examples/torus.ts';
import frustum from '../examples/frustum.ts';
import knot, { trefoil } from '../examples/knot.ts';
import cone from '../examples/cone.ts';
import capsule from '../examples/capsule.ts';
import elbow from '../examples/elbow.ts';
import spring from '../examples/spring.ts';
import squaretorus from '../examples/squaretorus.ts';

const { PI } = Math;

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

test('cone with a pole end is pi r^2 h / 3 within 0.5%', () => check('cone', cone, (PI * 1.5) / 3, 0.005));

test('lathe capsule is a cylinder plus a sphere within 0.5%', () => check('capsule', capsule, PI * 0.16 * 1.2 + (4 / 3) * PI * 0.064, 0.005));

test('quarter elbow obeys Pappus: pi r^2 R pi/2 within 0.5%', () => check('elbow', elbow, PI * 0.0625 * (PI / 2), 0.005));

test('coil spring volume is unchanged by the pitch: pi r^2 2 pi R turns within 1%', () =>
  check('spring', spring, PI * 0.0064 * 2 * PI * 0.6 * 6, 0.01));

test('twisted square torus keeps the Pappus volume 0.5 * 2 pi R within 0.5%', () => check('squaretorus', squaretorus, 0.5 * 2 * PI * 2, 0.005));
