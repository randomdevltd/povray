'use strict';
// node --test extensions/vscode/test/generate.test.mjs — the committed artifacts must match the generator.

import { test } from 'node:test';
import assert from 'node:assert';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const targets = ['extensions/vscode/syntaxes/pov.tmLanguage.json', 'extensions/vscode/pov4-blocks.json'];

test('generated artifacts are up to date', () => {
    const before = Object.fromEntries(targets.map((t) => [t, readFileSync(join(root, t), 'utf8')]));
    execFileSync(process.execPath, [join(root, 'tools/language/vscode.mjs')]);
    for (const t of targets)
        assert.equal(readFileSync(join(root, t), 'utf8'), before[t], `${t} differs from generator output`);
});

test('the grammar covers the shared word classes', () => {
    const grammar = JSON.parse(readFileSync(join(root, targets[0]), 'utf8'));
    assert.match(grammar.repository.statement.match, /\blet\b/);
    assert.match(grammar.repository.block.match, /\bsphere\b/);
    assert.match(grammar.repository.item.match, /\bambient\b/);
    assert.match(grammar.repository.builtin.match, /\bvlength\b/);
    assert.match(grammar.repository.channel.match, /\btransmit\b/);
    assert.match(grammar.repository.colorop.match, /\brgbft\b/);
});


test('photon controls are completed and quality is highlighted', () => {
    const blocks = JSON.parse(readFileSync(join(root, targets[1]), 'utf8'));
    const grammar = JSON.parse(readFileSync(join(root, targets[0]), 'utf8'));
    assert.match(grammar.repository.item.match, /\bquality\b/);
    for (const word of ['quality', 'method'])
        assert.ok(blocks.blocks.global_settings.nested.photons.includes(word));
});

test('independent refraction fields expose their block items to completion', () => {
    const blocks = JSON.parse(readFileSync(join(root, targets[1]), 'utf8')).blocks;
    const grammar = JSON.parse(readFileSync(join(root, targets[0]), 'utf8'));
    assert.doesNotMatch(grammar.repository.block.match, /\bfield\b/);
    assert.match(grammar.repository.block.match, /\brefraction\b/);
    for (const word of ['function', 'detail', 'auto'])
        assert.ok(blocks.refraction.items.includes(word));
    assert.equal(blocks.field, undefined);
    for (const word of ['strength', 'field', 'pigment']) assert.ok(!blocks.refraction.items.includes(word));
});


test('volume lights complete the bare emission source selector', () => {
    const blocks = JSON.parse(readFileSync(join(root, targets[1]), 'utf8')).blocks;
    assert.ok(blocks.light.items.includes('emission'));
    assert.ok(blocks.light.items.includes('pigment'));
    assert.ok(blocks.light.items.includes('brightness'));
});


test('surface lights and bounce orders expose contextual items to completion', () => {
    const blocks = JSON.parse(readFileSync(join(root, targets[1]), 'utf8')).blocks;
    assert.ok(blocks.sphere.items.includes('light'));
    assert.ok(blocks.light.items.includes('bounce'));
    assert.deepEqual(blocks.bounce.items, ['passes', 'self']);
});

test('lumens helpers and light units are highlighted and a light_source takes no light block', () => {
    const grammar = JSON.parse(readFileSync(join(root, targets[0]), 'utf8'));
    const blocks = JSON.parse(readFileSync(join(root, targets[1]), 'utf8')).blocks;
    assert.match(grammar.repository.builtin.match, /\bscale_lumens\b/);
    for (const word of ['nits', 'lumens', 'brightness', 'samples', 'emission', 'attenuation'])
        assert.ok(blocks.light.items.includes(word), `light item '${word}'`);
    assert.ok(blocks.interior.items.includes('light'));
    assert.ok(!blocks.light_source.items.includes('light'));
});
