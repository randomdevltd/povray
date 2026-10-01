import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildMesh, volume } from '../src/index.ts';
import column from '../examples/column.ts';

test('twisted hexagonal column keeps the prism volume (3 sqrt3 / 2) r^2 h', () => {
  const v = volume(buildMesh(column, undefined, undefined, false)) ?? NaN;
  const want = ((3 * Math.sqrt(3)) / 2) * 0.25 * 3;
  console.log(`column: volume ${v.toFixed(6)} want ${want.toFixed(6)} err ${((100 * Math.abs(v - want)) / want).toFixed(3)}%`);
  assert.ok(Math.abs(v - want) / want < 0.005);
});
