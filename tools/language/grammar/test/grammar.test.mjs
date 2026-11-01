// SPDX-License-Identifier: AGPL-3.0-or-later
import { test } from 'node:test';
import { readFileSync, readdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const here = dirname(dirname(fileURLToPath(import.meta.url)));
const root = join(here, '../../..');
const read = (p) => readFileSync(join(root, p), 'utf8');
const listParserSources = () => readdirSync(join(root, 'source/parser'))
    .filter((f) => f.endsWith('.cpp'))
    .map((f) => `source/parser/${f}`);

test('pov4.json covers every named rule of the tree-sitter grammar', () => {
    const ts = JSON.parse(read('libraries/tree-sitter-pov4/src/grammar.json'));
    const ours = JSON.parse(read('tools/language/grammar/pov4.json'));
    assert.deepEqual(Object.keys(ours.rules).sort(), Object.keys(ts.rules).sort());
    assert.deepEqual(ours.externals.sort(), ts.externals.map((e) => e.name).sort());
});

test('blocks.json items are reserved or contextually matched words and its sources exist', () => {
    const keywords = new Set(JSON.parse(read('tools/language/keywords.json')).map((k) => k.word));
    const contextual = new Set();
    for (const f of listParserSources()) {
        for (const m of read(f).matchAll(/CurrentTokenText\(\)\s*==\s*"([a-z_][a-z0-9_]*)"/g))
            contextual.add(m[1]);
    }
    const blocks = JSON.parse(read('tools/language/grammar/blocks.json')).blocks;
    assert.ok(Object.keys(blocks).length >= 50, 'curated block count');
    for (const [word, b] of Object.entries(blocks)) {
        assert.ok(b.items.length > 0, `${word} has items`);
        for (const item of b.items) {
            const kind = keywords.has(item) ? 'reserved' : (contextual.has(item) ? 'text-matched' : null);
            assert.ok(kind, `${word} item '${item}' is ${kind ?? 'unknown to the classic parser'}`);
        }
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
    for (const w of ['texture', 'pigment', 'interior', 'light', 'translate', 'hollow']) assert.ok(blocks.sphere.items.includes(w));
    assert.ok(blocks.union.items.includes('sphere'));
    for (const w of ['photons', 'attenuation', 'emission', 'bounce']) assert.ok(blocks.light.items.includes(w), `light item '${w}'`);
    assert.deepEqual(blocks.bounce.items, ['passes', 'self']);
    for (const w of ['shadows', 'method']) assert.ok(!blocks.light.items.includes(w), `invalid light item '${w}'`);
    for (const w of ['function', 'detail', 'auto']) assert.ok(blocks.refraction.items.includes(w), `refraction item '${w}'`);
    for (const w of ['strength', 'field', 'pigment']) assert.ok(!blocks.refraction.items.includes(w), `invalid refraction item '${w}'`);
    for (const w of ['agate', 'granite', 'marble', 'wrinkles', 'turbulence']) assert.ok(blocks.pigment.items.includes(w));
    // Contextual keywords, matched by text in the parser, are items too.
    for (const w of ['prepared_media_cells', 'atmospheric_ior', 'atmospheric_dispersion', 'mesh_cache', 'interface_texture'])
        assert.ok(blocks.global_settings.items.includes(w), `global_settings item '${w}'`);
    for (const w of ['method', 'quality'])
        assert.ok(blocks.global_settings.nested.photons.includes(w), `photon item '${w}'`);
    assert.ok(!JSON.parse(read('tools/language/keywords.json')).some((k) => k.word === 'quality'));
    for (const w of ['refraction', 'refraction_detail']) assert.ok(!blocks.media.items.includes(w), `invalid media item '${w}'`);
    assert.ok(!blocks.global_settings.items.includes('refraction_detail'));
    assert.ok(!blocks.light_source.items.includes('light'), 'a light_source cannot take a light block');
    assert.ok(blocks.light_source.items.includes('shadowless'));
    assert.deepEqual(blocks.attenuation.items, ['cutoff', 'power', 'scale']);
});

test('nested items stay in their sub-block', () => {
    const blocks = JSON.parse(read('tools/language/grammar/blocks.json')).blocks;
    const misplaced = {
        light: { photons: ['reflection', 'refraction'] },
        sphere: { photons: ['collect', 'pass_through', 'target'], radiosity: ['importance'] },
        light_source: { photons: ['global'] },
        finish: { reflection: ['falloff', 'exponent'], irid: ['thickness'], subsurface: ['translucency'] },
        media: { scattering: ['eccentricity', 'extinction'] },
        camera: { bokeh: ['pigment'] },
        global_settings: { photons: ['count', 'gather'], radiosity: ['pretrace_start'], subsurface: ['samples'] },
        pigment: { screen: ['fallback'] },
    };
    for (const [word, subs] of Object.entries(misplaced))
        for (const [sub, items] of Object.entries(subs))
            for (const w of items) {
                assert.ok(blocks[word].nested[sub].includes(w), `${word} ${sub} item '${w}'`);
                assert.ok(!blocks[word].items.includes(w), `'${w}' misplaced directly in ${word}`);
            }
    assert.deepEqual(blocks.light.nested.photons, ['reflection', 'refraction']);
    assert.ok(!blocks.light_source.nested.photons.includes('target'), 'a light_source photons block has no target');
    // Words after a value position stay items of the block: charset utf8, filter all.
    for (const w of ['utf8', 'ascii']) assert.ok(blocks.global_settings.items.includes(w), `charset value '${w}'`);
    assert.ok(blocks.image_map.items.includes('all'));
    assert.ok(!('charset' in blocks.global_settings.nested));
    const header = read('source/parser/pov4schema.h');
    assert.match(header, /\{ "light", \{ "photons", kPov4NestedItems_\d+, 2 \} \}/);
});

test('generators are stable: regenerating changes nothing', () => {
    const paths = ['tools/language/grammar/pov4.json', 'tools/language/grammar/blocks.json',
        'source/parser/pov4schema.h', 'doc/language-4-grammar.md'];
    const before = Object.fromEntries(paths.map((p) => [p, read(p)]));
    for (const script of ['extract.mjs', 'blocks.mjs', 'render.mjs'])
        execFileSync(process.execPath, [join(here, script)], { cwd: here });
    for (const p of paths) assert.equal(read(p), before[p], `${p} regenerated differently`);
});
