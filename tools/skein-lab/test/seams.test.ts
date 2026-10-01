import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildMesh, fold, seamGap, shape } from '../src/index.ts';
import { examples } from '../examples/index.ts';
import { trefoil } from '../examples/knot.ts';

test('closed wraps have no seam gap', () => {
  for (const [name, sh] of Object.entries(examples)) {
    if (!sh.wrapU && !sh.wrapV) continue;
    const gap = seamGap(sh, buildMesh(sh, 64, 64, false));
    console.log(`${name}: seam gap ${gap.toExponential(2)}`);
    assert.ok(gap < 1e-9, `${name} seam gap ${gap}`);
  }
});

test('without the holonomy correction the knot tube does not close', () => {
  const sh = shape({ wrap: 'uv' }, fold({ axis: trefoil, radius: 0.05, correct: false }));
  const gap = seamGap(sh, buildMesh(sh, 64, 64, false));
  console.log(`knot without holonomy correction: seam gap ${gap.toFixed(4)}`);
  assert.ok(gap > 1e-3);
});
