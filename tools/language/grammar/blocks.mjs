#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
// Mines the classic parser's EXPECT/CASE loops into blocks.json (the legal items of each
// scene-language block, plus nested sub-blocks) and pov4schema.h for the 4.0 checker.
// --inspect prints every mined function instead of writing files.
import { readFileSync, writeFileSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const parserDir = join(here, '../../../source/parser');

// Block word -> functions whose expect-loops define the legal items. A block only gets a
// schema when every listed function is found; anything absent is skipped (checked unchecked).
// Plain shape keywords share the object-modifier loop; CSG words also nest child objects.
const SHAPE_BLOCKS = ['bicubic_patch', 'box', 'cone', 'cubic', 'cylinder', 'disc', 'height_field',
    'isosurface_mesh', 'lemon', 'mesh', 'ovus', 'parametric', 'plane', 'poly', 'polygon',
    'polynomial', 'prism', 'quadric', 'quartic', 'smooth_triangle', 'sor', 'sphere',
    'superellipsoid', 'text', 'torus', 'triangle'];
const CSG_BLOCKS = ['composite', 'difference', 'intersection', 'light_group', 'merge', 'union'];
const WITH_OWN_ITEMS = {
    sphere_sweep: 'Parse_Sphere_Sweep', lathe: 'Parse_Lathe', prism: 'Parse_Prism', blob: 'Parse_Blob',
    mesh: 'Parse_Mesh1', mesh2: 'Parse_Mesh2', parametric: 'Parse_Parametric',
    bicubic_patch: 'Parse_Bicubic_Patch', julia_fractal: 'Parse_Julia_Fractal', isosurface: 'Parse_Isosurface_Body',
    isosurface_mesh: 'Parse_Isosurface_Body', portal: 'Parse_Portal', ovus: 'Parse_Ovus',
    cone: 'Parse_Cone', cylinder: 'Parse_Cylinder', lemon: 'Parse_Lemon', sor: 'Parse_Sor',
    text: 'Parse_TrueType',
};
const CURATION = {
    ...Object.fromEntries(SHAPE_BLOCKS.map((w) => [w, ['Parse_Object_Mods']])),
    ...Object.fromEntries(CSG_BLOCKS.map((w) => [w, ['Parse_Object', 'Parse_Object_Mods']])),
    ...Object.fromEntries(Object.entries(WITH_OWN_ITEMS).map(([w, fn]) => [w, [fn, 'Parse_Object_Mods']])),
    finish: ['Parse_Finish'],
    texture: ['Parse_Texture', 'Parse_Pattern'],
    material: ['Parse_Material'],
    pigment: ['Parse_Pigment', 'Parse_Pattern', 'ParseDensityFilePattern', 'ParsePotentialPattern'],
    normal: ['Parse_Tnormal', 'Parse_Pattern', 'ParseDensityFilePattern', 'ParsePotentialPattern'],
    density: ['Parse_Pigment', 'Parse_Pattern', 'ParseDensityFilePattern', 'ParsePotentialPattern'],
    interior: ['Parse_Interior'],
    refraction: ['Parse_Interior_Refraction'],
    media: ['Parse_Media'],
    light: ['Parse_Light'],
    bounce: ['Parse_Light_Bounce'],
    attenuation: ['Parse_Light_Attenuation'],
    camera: ['Parse_Camera', 'Parse_Camera_Mods', 'Parse_Camera_Look_At'],
    output: ['Parse_Camera_Output'],
    tags: ['Parse_Tags'],
    global_settings: ['Parse_Global_Settings'],
    object: ['Parse_Object', 'Parse_Object_Mods'],
    light_source: ['Parse_Light_Source', 'Parse_Object_Mods'],
    height_field: ['Parse_HField', 'Parse_Image', 'Parse_Object_Mods'],
    smooth_triangle: ['Parse_Smooth_Triangle', 'Parse_Three_UVCoords', 'Parse_Object_Mods'],
    image_map: ['Parse_Image_Map', 'Parse_Image'],
    bump_map: ['Parse_Bump_Map', 'Parse_Image'],
};

// Items the shared object-modifier loop mines for a block whose own parser refuses them: a light_source
// takes the modifier loop, but a surface `light { }` there is an error in the classic parser.
const EXCLUDED = { light_source: ['light'] };

const pad = (s) => s.replace(/[^\n]/g, ' ');
// Single-pass context scanner: blanks comments and literals without letting their
// contents open phantom contexts (a "/*" inside a string, a quote inside a comment).
const strip = (text) => {
    let out = '';
    let i = 0;
    while (i < text.length) {
        const c = text[i];
        if ((c === '/') && (text[i + 1] === '/')) {
            const end = text.indexOf('\n', i);
            out += pad(text.slice(i, end < 0 ? text.length : end));
            i = end < 0 ? text.length : end;
        } else if ((c === '/') && (text[i + 1] === '*')) {
            const end = text.indexOf('*/', i + 2);
            const stop = end < 0 ? text.length : end + 2;
            out += pad(text.slice(i, stop));
            i = stop;
        } else if ((c === '"') || (c === '\'')) {
            let j = i + 1;
            while ((j < text.length) && (text[j] !== c) && (text[j] !== '\n')) {
                if (text[j] === '\\') ++j;
                ++j;
            }
            out += pad(text.slice(i, Math.min(j + 1, text.length)));
            i = j + 1;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
};

const MACRO = /\b(EXPECT_ONE_CAT|EXPECT_ONE|EXPECT_CAT|EXPECT|END_EXPECT|CASE_EXPRESS_UNGET|CASE[A-Z_0-9]*|END_CASE|OTHERWISE|AllowToken|ALLOW|GET|POV4CTX)\b\s*(\(([^()]*)\))?/g;

function mineFunction(name, body) {
    const top = new Set();
    const nested = new Map();
    const groups = [];
    const arms = [];
    let m;
    MACRO.lastIndex = 0;
    while ((m = MACRO.exec(body)) !== null) {
        const [, macro, , args] = m;
        if (macro === 'POV4CTX') {
            // A contextual keyword: matched by text, absent from the reserved-word table, so it
            // travels as a raw word and words() passes it through.
            const item = '\x00' + (args ?? '').trim();
            if (groups.length > 0) groups.at(-1).items.add(item);
            else top.add(item);
        } else if (macro === 'EXPECT' || macro === 'EXPECT_ONE' || macro === 'EXPECT_ONE_CAT' || macro === 'EXPECT_CAT') {
            let owner = null;
            let block = false;
            for (let k = arms.length - 1; k >= 0; --k)
                if (arms[k].token && wordsOf.has(arms[k].token)) {
                    // A sub-block opens a brace between its arm and its loop; a value position does not.
                    const lead = body.slice(arms[k].at, m.index);
                    owner = arms[k].token;
                    block = (lead.match(/\bParse_Begin\b/g) ?? []).length > (lead.match(/\bParse_End\b/g) ?? []).length;
                    break;
                }
            groups.push({ owner, block, items: new Set() });
        } else if (macro === 'END_EXPECT') {
            const g = groups.pop();
            if (g) {
                for (const item of g.items) {
                    if (g.owner) {
                        if (!nested.has(g.owner)) nested.set(g.owner, { block: false, items: new Set() });
                        nested.get(g.owner).block = nested.get(g.owner).block || g.block;
                        nested.get(g.owner).items.add(item);
                    } else if (groups.length === 0) {
                        top.add(item);
                    } else {
                        groups.at(-1).items.add(item);
                    }
                }
            }
        } else if (macro.startsWith('CASE')) {
            const tokens = (args ?? '').split(',').map((t) => t.trim()).filter((t) => /^[A-Z0-9_]+_TOKEN$/.test(t));
            if (groups.length > 0) for (const t of tokens) groups.at(-1).items.add(t);
            arms.push({ token: tokens[0] ?? null, at: m.index });
        } else if (macro === 'END_CASE') {
            arms.pop();
        } else if (macro === 'OTHERWISE') {
            arms.push({ token: null, at: m.index });
        } else if ((macro === 'AllowToken') || (macro === 'ALLOW') || (macro === 'GET')) {
            const t = (args ?? '').trim();
            if (!/^[A-Z0-9_]+_TOKEN$/.test(t))
                continue;
            if (groups.length > 0)
                groups.at(-1).items.add(t);
            else
                top.add(t);
        }
    }
    return { top, nested };
}

function functionsOfFile(text) {
    const out = [];
    const defn = /^\S.*\bParser::(Parse\w+)\s*\(/gm;
    let m;
    while ((m = defn.exec(text)) !== null) {
        let i = text.indexOf('{', m.index);
        let depth = 0;
        for (; i < text.length; ++i) {
            if (text[i] === '{') ++depth;
            else if (text[i] === '}') { --depth; if (depth === 0) break; }
        }
        out.push({ name: m[1], line: text.slice(0, m.index).split('\n').length, body: text.slice(text.indexOf('{', m.index), i) });
    }
    return out;
}

const files = readdirSync(parserDir).filter((f) => f.endsWith('.cpp')).sort();
const keywords = JSON.parse(readFileSync(join(here, '../keywords.json'), 'utf8'));
const wordsOf = new Map();
for (const k of keywords) {
    if (!wordsOf.has(k.token)) wordsOf.set(k.token, []);
    wordsOf.get(k.token).push(k.word);
}

const inventory = new Map();
const sources = new Map();
// Contextual keywords never reach the expect-loops as tokens: the parser matches their text
// (CurrentTokenText() == "word") inside OTHERWISE branches. Rewrite each comparison into a
// synthetic macro call before stripping, so the scanner mines them like any other item.
const CTX = /CurrentTokenText\(\)\s*==\s*"([a-z_][a-z0-9_]*)"/g;
for (const file of files) {
    const raw = readFileSync(join(parserDir, file), 'utf8').replace(CTX, 'POV4CTX($1)');
    const text = strip(raw);
    for (const fn of functionsOfFile(text)) {
        inventory.set(fn.name, { name: fn.name, ...mineFunction(fn.name, fn.body), file, line: fn.line });
    }
}

const words = (tokens) => [...tokens].flatMap((t) => wordsOf.get(t) ?? (t.startsWith('\x00') ? [t.slice(1)] : [])).sort();
const clean = (set) => [...new Set(words(set))];

if (process.argv.includes('--inspect')) {
    const rows = [...inventory.entries()].filter(([, r]) => r.top.size > 0).sort((a, b) => a[0].localeCompare(b[0]));
    for (const [name, r] of rows)
        console.log(`${name} (${r.file}:${r.line})\n  items: ${clean(r.top).join(' ') || '(none reserved)'}\n` +
            [...r.nested.entries()].map(([p, g]) => `  ${g.block ? 'nested' : 'value'} ${(wordsOf.get(p) ?? [p])[0]}: ${clean(g.items).join(' ')}`).join('\n'));
    process.exit(0);
}

const blocks = {};
for (const [word, fns] of Object.entries(CURATION)) {
    const found = fns.map((f) => inventory.get(f));
    if (found.some((f) => !f)) { console.error(`skip ${word}: missing ${fns.filter((f) => !inventory.has(f)).join(', ')}`); continue; }
    const items = new Set();
    const nested = {};
    const nestedBy = {};
    for (const f of found) {
        for (const w of clean(f.top)) items.add(w);
        for (const [parent, g] of f.nested) {
            // Words after a value position are items of this block; a sub-block's items are not,
            // and a sub-block with its own schema is checked by that schema.
            if (!g.block) { for (const w of clean(g.items)) items.add(w); continue; }
            for (const p of wordsOf.get(parent) ?? []) {
                // The block's own parser runs first, so its sub-block shadows a shared loop's.
                if (CURATION[p] || ((nestedBy[p] ?? f) !== f)) continue;
                nestedBy[p] = f;
                nested[p] = nested[p] ?? new Set();
                for (const w of clean(g.items)) nested[p].add(w);
            }
        }
    }
    for (const w of EXCLUDED[word] ?? []) items.delete(w);
    blocks[word] = {
        items: [...items].sort(),
        nested: Object.fromEntries(Object.entries(nested).map(([p, s]) => [p, [...s].sort()])),
        sources: found.map((f) => `${f.file}:${f.line} ${f.name}`),
    };
}

const document = {
    format: 'pov4-blocks/1',
    comment: 'Legal reserved-word items of each scene-language block, mined from the classic parser expect-loops by tools/language/grammar/blocks.mjs. Nested lists the items of sub-blocks such as reflection { ... } inside finish. Non-reserved tokens (punctuation, expression starts) are not listed. Regenerate rather than edit.',
    blocks,
};
writeFileSync(join(here, 'blocks.json'), JSON.stringify(document, null, 1) + '\n');

const rows = Object.entries(blocks).sort(([a], [b]) => a.localeCompare(b));
let header = '// Generated by tools/language/grammar/blocks.mjs from the classic parser expect-loops. Do not edit.\n';
header += '#pragma once\n#include <cstddef>\n#include <cstring>\n#include <string>\n\nnamespace pov_parser {\n\n';
const defs = rows.map(([word, b]) => {
    const id = word.replace(/[^a-z0-9_]/g, '_');
    return `static const char* const kPov4BlockItems_${id}[] = { ${b.items.map((w) => `"${w}"`).join(', ')} };\n`;
}).join('');
const nestedEntriesUnions = new Map();
for (const [, b] of rows)
    for (const [p, items] of Object.entries(b.nested))
        nestedEntriesUnions.set(p, [...new Set([...(nestedEntriesUnions.get(p) ?? []), ...items])].sort());
// Per-parent sub-block items, so an item is legal only inside the sub-block of the block that owns it.
const nestedArrays = new Map();
const nestedRows = [];
for (const [word, b] of rows)
    for (const [p, items] of Object.entries(b.nested)) {
        const key = items.join(' ');
        if (!nestedArrays.has(key)) nestedArrays.set(key, { id: `kPov4NestedItems_${nestedArrays.size}`, items });
        nestedRows.push({ word, p, id: nestedArrays.get(key).id, count: items.length });
    }
const nestedArrayDefs = [...nestedArrays.values()].map(({ id, items }) =>
    `static const char* const ${id}[] = { ${items.map((w) => `"${w}"`).join(', ')} };\n`).join('');
const nestedEntries = [...nestedEntriesUnions.entries()];
const nestedDefs = nestedEntries.map(([p, items]) => {
    const id = p.replace(/[^a-z0-9_]/g, '_');
    return `static const char* const kPov4BlockItems_${id}[] = { ${items.map((w) => `"${w}"`).join(', ')} };\n`;
});
const table = `struct Pov4BlockSchema { const char* word; const char* const* items; size_t count; };\n\n` +
    `static const Pov4BlockSchema kPov4BlockSchemas[] = {\n` +
    rows.map(([word]) => {
        const id = word.replace(/[^a-z0-9_]/g, '_');
        return `    { "${word}", kPov4BlockItems_${id}, sizeof(kPov4BlockItems_${id}) / sizeof(kPov4BlockItems_${id}[0]) },`;
    }).join('\n') + (nestedEntries.length > 0 ? '\n' + nestedEntries.map(([p]) => {
        const id = p.replace(/[^a-z0-9_]/g, '_');
        return `    { "${p}", kPov4BlockItems_${id}, sizeof(kPov4BlockItems_${id}) / sizeof(kPov4BlockItems_${id}[0]) },`;
    }).join('\n') : '') +
    `\n};\n\ninline const Pov4BlockSchema* FindPov4BlockSchema(const char* word, size_t len)\n{\n` +
    `    for (const Pov4BlockSchema& s : kPov4BlockSchemas)\n        if ((std::strlen(s.word) == len) && (std::memcmp(s.word, word, len) == 0)) return &s;\n    return nullptr;\n}\n\n` +
    `struct Pov4NestedSchema { const char* parent; Pov4BlockSchema block; };\n\n` +
    `static const Pov4NestedSchema kPov4NestedSchemas[] = {\n` +
    nestedRows.map(({ word, p, id, count }) => `    { "${word}", { "${p}", ${id}, ${count} } },`).join('\n') +
    `\n};\n\n// The sub-block of parent named word, or null when parent has no such sub-block.\n` +
    `inline const Pov4BlockSchema* FindPov4NestedSchema(const std::string& parent, const std::string& word)\n{\n` +
    `    for (const Pov4NestedSchema& s : kPov4NestedSchemas)\n        if ((parent == s.parent) && (word == s.block.word)) return &s.block;\n    return nullptr;\n}\n\n` +
    `// The sub-blocks of parent that take item, joined by " or ", or empty when none does.\n` +
    `inline std::string Pov4NestedOwners(const std::string& parent, const std::string& item)\n{\n` +
    `    std::string owners;\n    for (const Pov4NestedSchema& s : kPov4NestedSchemas)\n        if (parent == s.parent)\n` +
    `            for (size_t i = 0; i < s.block.count; ++i)\n                if (item == s.block.items[i])\n` +
    `                {\n                    owners += (owners.empty() ? "" : " or ") + std::string(s.block.word);\n                    break;\n                }\n` +
    `    return owners;\n}\n\n} // namespace pov_parser\n`;
writeFileSync(join(here, '../../../source/parser/pov4schema.h'), header + defs + nestedDefs.join('') + nestedArrayDefs + table);
console.log(`blocks.json: ${rows.length} blocks, ${rows.reduce((n, [, b]) => n + b.items.length, 0)} items`);
