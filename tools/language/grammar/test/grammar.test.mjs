// SPDX-License-Identifier: AGPL-3.0-or-later
import { test } from 'node:test';
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const here = dirname(dirname(fileURLToPath(import.meta.url)));
const root = join(here, '../../..');
const read = (p) => readFileSync(join(root, p), 'utf8');

test('pov4.json covers every named rule of the tree-sitter grammar', () => {
    const ts = JSON.parse(read('libraries/tree-sitter-pov4/src/grammar.json'));
    const ours = JSON.parse(read('tools/language/grammar/pov4.json'));
    assert.deepEqual(Object.keys(ours.rules).sort(), Object.keys(ts.rules).sort());
    assert.deepEqual(ours.externals.sort(), ts.externals.map((e) => e.name).sort());
});

test('blocks.json items are reserved words and its sources exist', () => {
    const keywords = new Set(JSON.parse(read('tools/language/keywords.json')).map((k) => k.word));
    const blocks = JSON.parse(read('tools/language/grammar/blocks.json')).blocks;
    assert.ok(Object.keys(blocks).length >= 50, 'curated block count');
    for (const [word, b] of Object.entries(blocks)) {
        assert.ok(b.items.length > 0, `${word} has items`);
        for (const item of b.items) assert.ok(keywords.has(item), `${word} item '${item}' is a reserved word`);
        for (const src of b.sources) {
            const [file, rest] = src.split(':');
            const text = read(`source/parser/${file}`);
            assert.ok(new RegExp(`Parser::${rest.split(' ')[1]}\\s*\\(`).test(text), `${word} source ${src} names a real function`);
        }
    }
});

test('mined keyword sets match known parser content', () => {
    const blocks = JSON.parse(read('tools/language/grammar/blocks.json')).blocks;
    for (const w of ['ambient', 'diffuse', 'phong', 'reflection', 'specular']) assert.ok(blocks.finish.items.includes(w));
    assert.ok(blocks.finish.nested.reflection.includes('fresnel'));
    for (const w of ['location', 'look_at', 'right', 'up', 'perspective']) assert.ok(blocks.camera.items.includes(w));
    for (const w of ['texture', 'pigment', 'interior', 'translate', 'hollow']) assert.ok(blocks.sphere.items.includes(w));
    assert.ok(blocks.union.items.includes('sphere'));
    for (const w of ['agate', 'granite', 'marble', 'wrinkles', 'turbulence']) assert.ok(blocks.pigment.items.includes(w));
});

test('generators are stable: regenerating changes nothing', () => {
    const paths = ['tools/language/grammar/pov4.json', 'tools/language/grammar/blocks.json',
        'source/parser/pov4schema.h', 'doc/language-4-grammar.md'];
    const before = Object.fromEntries(paths.map((p) => [p, read(p)]));
    for (const script of ['extract.mjs', 'blocks.mjs', 'render.mjs'])
        execFileSync(process.execPath, [join(here, script)], { cwd: here });
    for (const p of paths) assert.equal(read(p), before[p], `${p} regenerated differently`);
});
