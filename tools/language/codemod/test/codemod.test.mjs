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

for (const scene of ['declarations', 'control', 'values', 'expressions', 'framing', 'tags', 'photons', 'media_light']) {
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

test('migrates media light colour and fade expressions independent of item order', () => {
  const base = mkdtempSync(join(tmpdir(), 'codemod-light-'));
  const path = join(base, 'lamp.pov');
  writeFileSync(path, '#declare Glow = rgb <1,0.5,0.2>;\n#declare Falloff = 2;\n' +
    'sphere { 0,1 interior { media { light_source { fade_power Falloff fade_distance Falloff samples 16 } emission Glow } } }\n');
  const [result] = new Program().convertScene(path);
  assert.deepEqual(result.refusals, []);
  assert.match(result.text, /light \{ pigment \{ color Glow \} samples 16/);
  assert.match(result.text, /media \{ emission Glow/);
  assert.match(result.text, /dropped fade_power Falloff fade_distance Falloff/);
});

test('refuses multiple legacy sources sharing one interior', () => {
  const base = mkdtempSync(join(tmpdir(), 'codemod-lights-'));
  const path = join(base, 'lamps.pov');
  writeFileSync(path, 'sphere { 0,1 interior { media { emission rgb 1 light_source {} } ' +
    'media { emission rgb 2 light_source {} } } }');
  const [result] = new Program().convertScene(path);
  assert.equal(result.text, null);
  assert.ok(result.refusals.every((r) => r.reason === 'multiple-media-lights'));
  assert.ok(result.refusals.length > 0);
});

test('moves signed and expression media refraction into independent interior blocks', () => {
  const base = mkdtempSync(join(tmpdir(), 'codemod-refraction-'));
  const path = join(base, 'refraction.pov');
  writeFileSync(path, '#version 3.7;\n#declare K = 2;\n' +
    'sphere { 0,1 interior { media { refraction -K * (1 + 2) density { spherical scale K } refraction_detail (K + 3) } } }\n' +
    'sphere { 0,1 interior { media { refraction +0.5 } } }\n');
  const [result] = new Program().convertScene(path);
  assert.deepEqual(result.refusals, []);
  assert.match(result.text, /interior \{ refraction \{ strength -K \* \(1 \+ 2\) field \{ pigment \{ spherical scale K \} \} detail \(\(K \+ 3\)\) \/ 10 \} \}/);
  assert.match(result.text, /refraction \{ strength \+0\.5 \}/);
  assert.equal(result.notes.filter((note) => note.note === 'media-refraction-overlap').length, 2);
  assert.doesNotMatch(result.text, /\bmedia\b|refraction_detail|\bdensity\b/);
});

test('retains coefficient media density and transport alongside refraction and a light', () => {
  const base = mkdtempSync(join(tmpdir(), 'codemod-refraction-light-'));
  const path = join(base, 'refraction.pov');
  writeFileSync(path, 'sphere { 0,1 interior { media { method 3 samples 12 absorption rgb 0.1 ' +
    'scattering { 1, rgb 0.2 } emission rgb 0.3 density { spherical } ' +
    'refraction -0.4 refraction_detail 7 light_source { samples 8 } } } }');
  const [result] = new Program().convertScene(path);
  assert.deepEqual(result.refusals, []);
  assert.match(result.text, /media \{ method 3 samples 12 absorption rgb 0\.1 scattering \{ 1, rgb 0\.2 \} emission rgb 0\.3 density \{ spherical \}/);
  assert.match(result.text, /light \{ pigment \{ pigment_pattern \{ spherical \}/);
  assert.match(result.text, /refraction \{ strength -0\.4 field \{ pigment \{ spherical \} \} detail \(7\) \/ 10 \}/);
  assert.doesNotMatch(result.text, /refraction_detail/);
});

test('migrates default media refraction and converts moved field expressions', () => {
  const base = mkdtempSync(join(tmpdir(), 'codemod-refraction-default-'));
  const path = join(base, 'refraction.pov');
  writeFileSync(path, '#declare let = 2;\n#default { media { refraction let density { function { x = y } } } }');
  const [result] = new Program().convertScene(path);
  assert.deepEqual(result.refusals, []);
  assert.match(result.text, /default \{ interior \{ refraction \{ strength let_ field \{ pigment \{ function \{ x == y \} \} \} \} \} \}/);
});

test('converts legacy detail millimetres with a literal scene scale and preserves auto', () => {
  const base = mkdtempSync(join(tmpdir(), 'codemod-refraction-units-'));
  const path = join(base, 'refraction.pov');
  writeFileSync(path, 'global_settings { mm_per_unit 25 }\n' +
    'sphere { 0,1 interior { media { refraction 0.1 refraction_detail 5 } } } ' +
    'sphere { 0,1 interior { media { refraction 0.2 refraction_detail auto } } }');
  const [result] = new Program().convertScene(path);
  assert.deepEqual(result.refusals, []);
  assert.match(result.text, /strength 0\.1 detail \(5\) \/ 25/);
  assert.match(result.text, /strength 0\.2 detail auto/);
});

test('refuses ambiguous scene scaling for legacy refraction detail', () => {
  const base = mkdtempSync(join(tmpdir(), 'codemod-refraction-units-'));
  const path = join(base, 'refraction.pov');
  writeFileSync(path, '#declare Units = 25; global_settings { mm_per_unit Units }\n' +
    'sphere { 0,1 interior { media { refraction 0.1 refraction_detail 5 } } }');
  const [result] = new Program().convertScene(path);
  assert.equal(result.text, null);
  assert.deepEqual(result.refusals.map((r) => r.reason), ['refraction-detail-units']);
});

for (const coefficient of ['absorption rgb 0.1', 'emission rgb 0.2', 'scattering { 1, rgb 0.3 }']) {
  test(`retains density and transport for ${coefficient.split(' ')[0]} media`, () => {
    const base = mkdtempSync(join(tmpdir(), 'codemod-refraction-coefficient-'));
    const path = join(base, 'refraction.pov');
    writeFileSync(path, `sphere { 0,1 interior { media { samples 16 ${coefficient} refraction 0.1 density { spherical } } } }`);
    const [result] = new Program().convertScene(path);
    assert.deepEqual(result.refusals, []);
    assert.ok(result.text.includes(`media { samples 16 ${coefficient} density { spherical } }`));
    assert.match(result.text, /refraction \{ strength 0\.1 field \{ pigment \{ spherical \} \} \}/);
  });
}

test('inherits global detail and clones only transforms that affect the density', () => {
  const base = mkdtempSync(join(tmpdir(), 'codemod-refraction-inherit-'));
  const path = join(base, 'refraction.pov');
  writeFileSync(path, 'global_settings { mm_per_unit 20 refraction_detail 6 }\n' +
    'sphere { 0,1 interior { media { absorption rgb 1 refraction 0.1 scale 2 density { spherical } translate x scale 3 } } }');
  const [result] = new Program().convertScene(path);
  assert.deepEqual(result.refusals, []);
  assert.match(result.text, /media \{ absorption rgb 1 scale 2 density \{ spherical \} translate x scale 3 \}/);
  assert.match(result.text, /field \{ pigment \{ spherical translate x scale 3 \} \} detail \(6\) \/ 20/);
});

for (const [contents, reason] of [
  ['refraction 1 density { spherical } density { bozo }', 'refraction-density-product'],
  ['KnownMedium refraction 1', 'refraction-media-identity'],
  ['refraction 1 mix max', 'refraction-mixing'],
  ['refraction 1 priority 2', 'refraction-mixing'],
]) {
  test(`refuses ambiguous legacy media: ${reason}`, () => {
    const base = mkdtempSync(join(tmpdir(), 'codemod-refraction-refuse-'));
    const path = join(base, 'refraction.pov');
    writeFileSync(path, `sphere { 0,1 interior { media { ${contents} } } }`);
    const [result] = new Program().convertScene(path);
    assert.equal(result.text, null);
    assert.ok(result.refusals.some((r) => r.reason === reason));
  });
}

test('refuses multiple refractive media sharing one interior', () => {
  const base = mkdtempSync(join(tmpdir(), 'codemod-refraction-multiple-'));
  const path = join(base, 'refraction.pov');
  writeFileSync(path, 'sphere { 0,1 interior { media { refraction 1 } media { refraction 2 } } }');
  const [result] = new Program().convertScene(path);
  assert.equal(result.text, null);
  assert.ok(result.refusals.every((r) => r.reason === 'multiple-media-refraction'));
  assert.equal(result.refusals.length, 2);
});
