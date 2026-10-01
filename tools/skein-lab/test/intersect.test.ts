import { test } from 'node:test';
import assert from 'node:assert/strict';
import { selfIntersections } from '../src/index.ts';
import { examples } from '../examples/index.ts';

test('a tube fatter than half the strand spacing is caught intersecting itself', () => {
  const n = selfIntersections(examples.fatknot);
  console.log(`fatknot: ${n} intersecting triangle pairs`);
  assert.ok(n > 0);
});

test('the clean examples report no self-intersections', () => {
  for (const name of ['sphere', 'torus', 'frustum', 'log', 'knot', 'curl'] as const) {
    const n = selfIntersections(examples[name]);
    console.log(`${name}: ${n}`);
    assert.equal(n, 0, name);
  }
});
