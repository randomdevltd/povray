// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { classify } from '../eval.mjs';
import { encodePng } from '../png.mjs';

const dir = mkdtempSync(join(tmpdir(), 'lang-eval-'));
writeFileSync(join(dir, 's.pov'), '#version 3.6;\n');
const planned = { run: dir, scenesDir: dir, only: ['A', 'B', 'C'] };
const flat = (v) => encodePng({ width: 4, height: 4, rgb: Buffer.alloc(48, v) });
const scene = (name, renders) => {
  const jobs = {};
  for (const [kind, [status, value]] of Object.entries(renders)) {
    jobs[kind] = { output: join(dir, `${name}.${kind}.png`), status: join(dir, `${name}.${kind}.json`) };
    if (value !== undefined) writeFileSync(jobs[kind].output, flat(value));
    writeFileSync(jobs[kind].status, JSON.stringify({ status, message: status === 'error' ? 'Parse Error' : '' }));
  }
  return { scene: 's.pov', version: '3.6', converted: true, refusals: [], jobs };
};

test('classifies scenes by their worst pair and status', () => {
  const run = (renders) => classify(planned, scene(Object.keys(renders).join(''), renders), join(dir, 'cache'));
  assert.equal(run({ A: ['ok', 9], B: ['ok', 9], C: ['ok', 9] }).classification, 'identical');
  const differs = run({ A: ['ok', 9], B: ['ok', 9], C: ['ok', 200] });
  assert.equal(differs.classification, 'different');
  assert.match(differs.message, /B≠C: 4\.0 defaults \(3\.6 scene without assumed_gamma/);
  assert.equal(run({ A: ['ok', 9], B: ['error'], C: ['ok', 9] }).classification, 'eval-error');
  assert.equal(run({ A: ['timeout'], C: ['ok', 9] }).classification, 'timeout');
  assert.equal(classify({ ...planned, only: ['A'] }, scene('only', { A: ['ok', 9] }), join(dir, 'cache')).classification, 'a-only');
});
