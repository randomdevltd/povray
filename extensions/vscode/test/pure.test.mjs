'use strict';
// node --test extensions/vscode/test/pure.test.mjs — no vscode dependency.

import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import pure from '../src/pure.js';

const here = dirname(fileURLToPath(import.meta.url));
const BLOCKS = JSON.parse(readFileSync(join(here, '..', 'pov4-blocks.json'), 'utf8')).blocks;

test('enclosingBlocks tracks nested blocks and skips strings and comments', () => {
    const text = 'sphere { // { not a block\n texture { pigment { rgb <1, "}" , 0> } } }';
    const at = text.indexOf('rgb');
    const known = new Set(Object.keys(BLOCKS));
    assert.deepEqual(pure.enclosingBlocks(text, at, known), ['sphere', 'texture', 'pigment']);
});

test('enclosingBlocks treats anonymous braces as null entries', () => {
    const text = 'let D = { a: 1 }; finish { ';
    const chain = pure.enclosingBlocks(text, text.length, new Set(Object.keys(BLOCKS)));
    assert.equal(chain[chain.length - 1], 'finish');
});

test('completionContext resolves nested sub-blocks against their parent', () => {
    const text = 'finish { reflection { ';
    const ctx = pure.completionContext(text, text.length, BLOCKS);
    assert.equal(ctx.block, 'reflection');
    assert.ok(ctx.items.includes('fresnel'));
    const finish = pure.completionContext('finish { ', 10, BLOCKS);
    assert.equal(finish.block, 'finish');
    assert.ok(finish.items.includes('phong'));
});

test('parseDiagnostics reads 4.0 file:line:col records and classic line records', () => {
    const log = [
        "File 'err_prop.pov4' line 2: Parse Error: err_prop.pov4:2:24: 'finish' has no item 'phon'; did you mean 'phong'?",
        'scene.pov4:3:3: A float cannot appear at the top level.',
        "Cannot open include file 'err_items.inc4' line 2.",
        'completely unrelated line',
        'scene.pov4:3:3: A float cannot appear at the top level.',
    ].join('\n');
    const found = pure.parseDiagnostics(log);
    assert.equal(found.length, 3);
    assert.deepEqual(found[0], { file: 'err_prop.pov4', line: 2, column: 24,
        message: "'finish' has no item 'phon'; did you mean 'phong'?" });
    assert.deepEqual(found[1], { file: 'scene.pov4', line: 3, column: 3,
        message: 'A float cannot appear at the top level.' });
    assert.equal(found[2].file, 'err_items.inc4');
    assert.equal(found[2].line, 2);
});

test('collectIdentifiers finds 4.0 and classic declarations', () => {
    const text = [
        'let Count = 3;',
        'fn Tinted(K) { return K; }',
        'global Mode = 1;',
        '#declare Sphere1 = sphere { }',
        '#local (Foo) = 2;',
        'Count = 4;',
    ].join('\n');
    assert.deepEqual(pure.collectIdentifiers(text).sort(), ['Count', 'Foo', 'Mode', 'Sphere1', 'Tinted']);
});

test('completionsFor offers block items inside blocks, blocks and snippets outside', () => {
    const inside = pure.completionsFor({ block: 'finish', items: BLOCKS.finish.items }, BLOCKS, []);
    assert.ok(inside.some((i) => (i.label === 'phong') && (i.detail === 'finish item')));
    const outside = pure.completionsFor({ block: null, items: [] }, BLOCKS, ['Mine']);
    assert.ok(outside.some((i) => i.label === 'sphere' && i.kind === 'block'));
    assert.ok(outside.some((i) => i.label === 'Mine' && i.kind === 'identifier'));
    assert.ok(outside.some((i) => i.kind === 'snippet'));
});
