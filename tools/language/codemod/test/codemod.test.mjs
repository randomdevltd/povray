// SPDX-License-Identifier: AGPL-3.0-or-later
// Fixture pairs: each classic file converts to the .pov4/.inc4 beside it; UPDATE=1 rewrites them.
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, join } from 'node:path';
import test from 'node:test';
import { applyEdits, Program } from '../convert.mjs';

const dir = join(import.meta.dirname, 'fixtures');
const expected = (path) => join(dir, basename(path).replace(/\.pov$/, '.pov4').replace(/\.inc$/, '.inc4'));
const check = (path, text) => {
  if (process.env.UPDATE) writeFileSync(expected(path), text, 'latin1');
  assert.equal(text, readFileSync(expected(path), 'latin1'));
};

for (const scene of ['declarations', 'control', 'values', 'expressions']) {
  test(`converts ${scene}.pov`, () => {
    const [result] = new Program().convertScene(join(dir, `${scene}.pov`));
    assert.deepEqual(result.refusals, []);
    check(result.path, result.text);
  });
}

test('converts reached includes and keeps unresolved ones classic', () => {
  const results = new Program({ includes: 'convert' }).convertScene(join(dir, 'includes.pov'));
  assert.deepEqual(results.map((r) => basename(r.path)), ['parts.inc', 'includes.pov']);
  for (const result of results) check(result.path, result.text);
  assert.deepEqual(results[0].notes.map((n) => n.note), ['include-local']);
  assert.deepEqual(results[1].notes.map((n) => n.note), ['classic-include']);
});

test('refuses what it cannot map, with locations', () => {
  const [result] = new Program().convertScene(join(dir, 'refused.pov'));
  assert.equal(result.text, null);
  assert.deepEqual(result.refusals.map((r) => `${r.line}:${r.reason}`), [
    '6:dynamic-scope', '13:by-reference-argument', '15:layered-texture', '17:switch-fallthrough', '23:splice-precedence',
  ]);
});

test('refuses a scene macro that a classic include calls', () => {
  const [scene] = new Program().convertScene(join(dir, 'callback.pov'));
  assert.deepEqual(scene.refusals.map((r) => r.reason), ['called-by-classic-include']);
});

test('refuses an include #local that would replace an including binding', () => {
  const [parts] = new Program({ includes: 'convert' }).convertScene(join(dir, 'shadow.pov'));
  assert.deepEqual(parts.refusals.map((r) => r.reason), ['include-local-shadow']);
});

test('an edit inside a wider one is superseded', () => {
  const edits = [{ start: 2, end: 3, str: 'X', seq: 0 }, { start: 0, end: 5, str: 'abc', seq: 1 },
    { start: 5, end: 5, str: ';', seq: 2 }];
  assert.equal(applyEdits('01234567', edits, 0, 8), 'abc;567');
  assert.throws(() => applyEdits('0123', [{ start: 0, end: 2, str: '', seq: 0 }, { start: 1, end: 3, str: '', seq: 1 }], 0, 4));
});

test('every fixture has an expected output', () => {
  const inputs = readdirSync(dir).filter((f) => /\.(pov|inc)$/.test(f) && !['refused.pov', 'shadow.pov', 'callback.pov', 'callback.inc'].includes(f));
  for (const f of inputs) assert.ok(readdirSync(dir).includes(basename(expected(f))), f);
});

test('names includes uniquely within one output folder', () => {
  const base = mkdtempSync(join(tmpdir(), 'codemod-'));
  for (const d of ['scenes', 'up', 'up2']) mkdirSync(join(base, d));
  for (const d of ['up', 'up2']) writeFileSync(join(base, d, 'u.inc'), '#declare U = 1;\n');
  writeFileSync(join(base, 'scenes', 'a.pov'), '#include "../up/u.inc"\n');
  writeFileSync(join(base, 'scenes', 'b.pov'), '#include "../up2/u.inc"\n');
  const program = new Program({ includes: 'convert' });
  const names = ['a', 'b'].map((s) => program.convertScene(join(base, 'scenes', `${s}.pov`), { folder: 'out' })[0].name);
  assert.deepEqual(names, ['u.inc4', 'u-2.inc4']);
});
