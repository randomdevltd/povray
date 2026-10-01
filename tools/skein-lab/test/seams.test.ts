import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildMesh, fold, parts, seamGap, shape } from '../src/index.ts';
import { examples } from '../examples/index.ts';
import { trefoil } from '../examples/knot.ts';

test('closed wraps have no seam gap', () => {
  let worst = 0, count = 0;
  for (const [name, model] of Object.entries(examples))
    for (const sh of parts(model)) {
      if (!sh.wrapU && !sh.wrapV) continue;
      const gap = seamGap(sh, buildMesh(sh, 64, 64, false));
      assert.ok(gap < 1e-9, `${name} seam gap ${gap}`);
      worst = Math.max(worst, gap);
      count++;
    }
  console.log(`${count} wrapped parts, worst seam gap ${worst.toExponential(2)}`);
});

test('without the holonomy correction the knot tube does not close', () => {
  const sh = shape({ wrap: 'uv' }, fold({ axis: trefoil, radius: 0.05, correct: false }));
  const gap = seamGap(sh, buildMesh(sh, 64, 64, false));
  console.log(`knot without holonomy correction: seam gap ${gap.toFixed(4)}`);
  assert.ok(gap > 1e-3);
});
