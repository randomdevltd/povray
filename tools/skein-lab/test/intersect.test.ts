import { test } from 'node:test';
import assert from 'node:assert/strict';
import { selfIntersections } from '../src/index.ts';
import { examples, failures } from '../examples/index.ts';

test('a tube fatter than half the strand spacing is caught intersecting itself', () => {
  const n = selfIntersections(examples.fatknot);
  console.log(`fatknot: ${n} intersecting triangle pairs`);
  assert.ok(n > 0);
});

test('the figure-eight Klein bottle is caught', () => {
  const n = selfIntersections(examples.klein);
  console.log(`klein: ${n} intersecting triangle pairs`);
  assert.ok(n > 0);
});

test('every example outside the failure gallery reports no self-intersections', () => {
  const dirty = Object.entries(examples).filter(([name]) => !failures.includes(name)).map(([name, m]) => [name, selfIntersections(m)] as const).filter(([, n]) => n > 0);
  console.log(`clean gallery: ${Object.keys(examples).length - failures.length} examples, dirty: ${JSON.stringify(dirty)}`);
  assert.deepEqual(dirty, []);
});
