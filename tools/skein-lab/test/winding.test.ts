import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildMesh, fold, orientation, shape, spherical, translate, unionVolume, volume, Y, Z } from '../src/index.ts';
import { examples } from '../examples/index.ts';
import type { Model } from '../src/index.ts';

const { PI } = Math;
const divergence = (m: Model) => (Array.isArray(m) ? m : [m]).reduce((a, sh) => a + (volume(buildMesh(sh, undefined, undefined, false)) ?? NaN), 0);

function check(name: string, model: Model, want: number, tol: number) {
  const u = unionVolume(model);
  const err = Math.abs(u.union - want) / want;
  console.log(`${name}: union ${u.union.toFixed(4)} want ${want.toFixed(4)} err ${(100 * err).toFixed(3)}%, divergence ${divergence(model).toFixed(4)}, winding ${u.minWinding}..${u.maxWinding}`);
  assert.ok(err < tol);
}

test('two unit balls one radius apart: union is two balls minus the lens, within 1%', () => {
  const ball = (x: number) => shape({ wrap: 'u', ends: 'pole' }, spherical(), translate([x, 0, 0]));
  check('two lobes', [ball(0), ball(1)], 2 * (4 / 3) * PI - (PI * 5 * 1) / 12, 0.01);
});

test('a tube bent 1.25 turns overlaps itself by a quarter: union is one torus, within 1%', () => {
  const sh = shape({ wrap: 'u', ends: 'flat' }, fold({ radius: 0.5 }), fold({ axis: Z, along: Y, radius: 2, range: 2.5 * PI }));
  check('1.25-turn tube', sh, 2 * PI * 2 * PI * 0.25, 0.01);
});

test('union equals divergence volume where nothing overlaps', () => {
  check('torus', examples.torus, divergence(examples.torus), 0.005);
});

test('fat knot: union volume is below its divergence volume', () => {
  const u = unionVolume(examples.fatknot).union, d = divergence(examples.fatknot);
  console.log(`fatknot: union ${u.toFixed(3)} divergence ${d.toFixed(3)}`);
  assert.ok(u < d);
});

test('orientability: torus yes, Klein no (closed), Möbius no (open)', () => {
  const o = (m: Model) => orientation(buildMesh((Array.isArray(m) ? m : [m])[0], 48, 48, false));
  const [t, k, mo] = [o(examples.torus), o(examples.klein), o(examples.mobius)];
  console.log({ torus: t, klein: k, mobius: mo });
  assert.ok(t.orientable && t.boundaryEdges === 0);
  assert.ok(!k.orientable && k.boundaryEdges === 0);
  assert.ok(!mo.orientable && mo.boundaryEdges > 0);
});
