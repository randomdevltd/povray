#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
// Scene-language lookup: `node tools/language/reference.mjs <term>` prints a card for a block or
// item; `--list` the index; `--generate` rebuilds reference/descriptions.json and the SKILL.md table.
import { readFileSync, writeFileSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '../..');
const dataDir = join(here, 'reference');
const readJSON = (p) => JSON.parse(readFileSync(p, 'utf8'));
const keywords = readJSON(join(here, 'keywords.json'));
const kwByWord = new Map(keywords.map((k) => [k.word, k]));
const blocks = readJSON(join(here, 'grammar/blocks.json')).blocks;
const overlay = readJSON(join(dataDir, 'overlay.json'));

function loadDescriptions() {
    return readJSON(join(dataDir, 'descriptions.json'));
}

function itemUniverse() {
    const words = new Set();
    for (const b of Object.values(blocks)) {
        for (const w of b.items) words.add(w);
        for (const list of Object.values(b.nested || {})) for (const w of list) words.add(w);
    }
    for (const w of Object.keys(blocks)) words.delete(w);
    for (const w of Object.keys(overlay.words)) words.delete(w);
    return words;
}

const overlayBlocks = () => Object.keys(overlay.words).filter((w) => overlay.words[w].kind === 'block').sort();

// ---------------------------------------------------------------- generation

const manualSections = () => {
    const out = [];
    const dir = join(repo, 'doc/html');
    for (const f of readdirSync(dir).filter((x) => /^r3_\d+\.html$/.test(x)).sort()) {
        const text = readFileSync(join(dir, f), 'utf8');
        const re = /<div class="content-level-h\d+"[^>]*contains="([^"]*)"[^>]*id="([^"]*)">\s*<h\d>([^<]*)<\/h\d>/g;
        const opens = [];
        let m;
        while ((m = re.exec(text)) !== null) opens.push({ title: m[3], id: m[2], index: m.index });
        for (let i = 0; i < opens.length; ++i) {
            const body = text.slice(opens[i].index, i + 1 < opens.length ? opens[i + 1].index : text.length);
            out.push({
                file: f,
                title: opens[i].title,
                id: opens[i].id,
                paras: [...body.matchAll(/<p>([\s\S]*?)<\/p>/g)].map((x) => x[1]),
            });
        }
    }
    return out;
};

const decode = (s) => s.replace(/<br\s*\/?>/g, ' ').replace(/<[^>]+>/g, '')
    .replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'").replace(/&amp;/g, '&').replace(/\s+/g, ' ').trim();

const DEFN = /keyword|statement|option|parameter|directive|controls|controlled|specifies|specified|sets|defines|defined|determines|gives|causes|indicates|consists|followed|used (in|to|with|for)|produces|applies|computes|returns|contains/i;

function mineWord(sections, word) {
    const variants = [word];
    const alt = word.replace(/colour/g, 'color').replace(/grey/g, 'gray');
    if (alt !== word) variants.push(alt);
    for (const v of variants) {
        const hit = mineVariant(sections, v);
        if (hit) return { text: hit.text, doc: `${hit.file}#${hit.id}` };
    }
    return null;
}

function mineVariant(sections, v) {
    const funcRe = new RegExp(`<code>${v}\\([^<]*\\)<\\/code>(?:<br\\s*\\/?>)?\\s*([\\s\\S]*?)(?:\\.|(?=<\\/p>))`);
    const codeAt = (s) => {
        const c = s.match(new RegExp(`<code>${v}($|[^a-z0-9_])`));
        if (c) return c.index;
        const e = s.match(new RegExp(`<em>${v}<\\/em>`));
        return e ? e.index : -1;
    };
    let best = null;
    for (const s of sections) {
        const titleHit = new RegExp(`(^|[^a-z0-9_])${v.replace(/_/g, '[ _]')}([^a-z0-9_]|$)`, 'i').test(s.title);
        for (const para of s.paras) {
            const fm = para.match(funcRe);
            if (fm) {
                const t = decode(fm[1]);
                if (t.length >= 4 && t.length <= 140 && /^[A-Z]/.test(t)) {
                    const sc = 5 + (titleHit ? 2 : 0);
                    if (!best || sc > best.sc) best = { sc, text: t + '.', file: s.file, id: s.id };
                }
            }
            for (const sent of para.split(/(?<=[.:])\s+/)) {
                const idx = codeAt(sent);
                if (idx < 0) continue;
                const t = decode(sent);
                if (!t || t.length < 12 || t.length > 200 || !/^[A-Z0-9"(]/.test(t) || /[:]$/.test(t)) continue;
                if (/\bcannot\b/.test(t)) continue;
                const sc = (idx < 80 ? 2 : 0) + (DEFN.test(t) ? 1 : 0) + (titleHit ? 2 : 0);
                if (sc >= 3 && (!best || sc > best.sc)) best = { sc, text: t, file: s.file, id: s.id };
            }
        }
    }
    return best;
}

const fixtureTexts = () => readdirSync(join(repo, 'tests/pov4'))
    .filter((f) => /\.(pov4|inc4)$/.test(f) && !/^(err_|bad_)/.test(f))
    .sort()
    .map((f) => ({ name: `tests/pov4/${f}`, text: readFileSync(join(repo, 'tests/pov4', f), 'utf8') }));

const manualPres = () => {
    const out = [];
    const dir = join(repo, 'doc/html');
    for (const f of readdirSync(dir).filter((x) => /^r3_\d+\.html$/.test(x)).sort()) {
        const text = readFileSync(join(dir, f), 'utf8');
        for (const m of text.matchAll(/<pre>([\s\S]*?)<\/pre>/g))
            out.push({ name: `doc/html/${f}`, text: m[1], noLines: true });
    }
    return out;
};

const decodeLine = (s) => s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"')
    .replace(/&amp;/g, '&').replace(/\s+/g, ' ').trim();
const lineOf = (text, index) => text.slice(0, index).split('\n').length;
const usable = (s, cap = 110) => s.length >= 10 && s.length <= cap && !/[#]|\/\//.test(s)
    && !/\[[-A-Z0-9_]+\]/.test(s);

function spanSnippet(sources, word, snippets) {
    const re = new RegExp(`(^|[^A-Za-z0-9_])${word}\\s*\\{`, 'g');
    for (const src of sources) {
        const flat = src.text.replace(/\n/g, ' ');
        let m;
        re.lastIndex = 0;
        while ((m = re.exec(flat)) !== null) {
            let depth = 0;
            let i = flat.indexOf('{', m.index);
            for (; i < flat.length; ++i) {
                if (flat[i] === '{') ++depth;
                else if (flat[i] === '}') { --depth; if (depth === 0) break; }
            }
            if (depth !== 0) break;
            const code = decodeLine(flat.slice(m.index + m[1].length, i + 1));
            const body = code.slice(code.indexOf('{') + 1, code.lastIndexOf('}'));
            if (usable(code) && body.trim().length > 0) {
                snippets[word] = { code, from: `${src.name}:${lineOf(src.text, m.index + m[1].length)}` };
                return;
            }
            re.lastIndex = i;
        }
    }
}

function lineSnippet(sourceGroups, word, snippets) {
    const re = new RegExp(`(^|[^A-Za-z0-9_])${word}([^A-Za-z0-9_]|$)`);
    for (const group of sourceGroups) {
        let best = null;
        for (const src of group) {
            const lines = src.text.split('\n');
            for (let i = 0; i < lines.length; ++i) {
                const code = decodeLine(lines[i]);
                if (code === word || !re.test(code) || !usable(code, 90)) continue;
                const rank = (code.includes('{') ? 0 : 1000) + code.length;
                if (!best || rank < best.rank) best = { rank, code, from: src.noLines ? src.name : `${src.name}:${i + 1}` };
            }
        }
        if (best) {
            snippets[word] = best;
            return;
        }
    }
}

function mineSnippets() {
    const snippets = {};
    const fixtures = fixtureTexts();
    const pres = manualPres();
    const blockNames = new Set(Object.keys(blocks));
    const overlayBlockNames = new Set(overlayBlocks());
    const words = new Set([...Object.keys(overlay.words), ...itemUniverse(), ...blockNames]);
    for (const w of words) {
        if (blockNames.has(w) || overlayBlockNames.has(w)) {
            spanSnippet(fixtures, w, snippets);
            if (!snippets[w]) spanSnippet(pres, w, snippets);
        } else {
            lineSnippet([fixtures, pres], w, snippets);
        }
    }
    return Object.fromEntries(Object.keys(snippets).sort().map((k) => [k, snippets[k]]));
}

const GENERATED_COMMENT = 'One-line descriptions and usage snippets mined by tools/language/reference.mjs --generate from the built manual (doc/html/r3_*.html) and the twin fixtures (tests/pov4). Descriptions carry the manual anchor they were taken from; absent words were not found. Block one-liners and 4.0-only words live in overlay.json. Regenerate rather than edit.';

export function generate() {
    const ownBlocks = Object.keys(overlay.blocks).sort();
    const minedBlocks = Object.keys(blocks).sort();
    if (JSON.stringify(ownBlocks) !== JSON.stringify(minedBlocks))
        throw new Error('overlay.json blocks do not match grammar/blocks.json block names');
    const sections = manualSections();
    const descriptions = {};
    for (const w of [...itemUniverse()].sort()) {
        const hit = mineWord(sections, w);
        if (hit) descriptions[w] = hit;
    }
    const snippets = mineSnippets();
    const doc = { format: 'pov4-descriptions/1', comment: GENERATED_COMMENT, descriptions, snippets };
    writeFileSync(join(dataDir, 'descriptions.json'), JSON.stringify(doc, null, 1) + '\n');
    const skillPath = join(dataDir, 'SKILL.md');
    const before = readFileSync(skillPath, 'utf8');
    if (!before.includes('<!-- generated:begin block table -->') || !before.includes('<!-- generated:end block table -->'))
        throw new Error('SKILL.md block-table markers not found');
    const after = before.replace(/(<!-- generated:begin block table -->)[\s\S]*(<!-- generated:end block table -->)/,
        `$1\n${blockTableMarkdown()}\n$2`);
    writeFileSync(skillPath, after);
    return { mined: Object.keys(descriptions).length, snippets: Object.keys(snippets).length };
}

// ---------------------------------------------------------------- rendering

function descriptionOf(word) {
    const hit = loadDescriptions().descriptions[word];
    return hit ? { text: hit.text, provenance: `doc: ${hit.doc}` } : null;
}

function blockCard(name) {
    const b = blocks[name];
    const kw = kwByWord.get(name);
    const lines = [`${name}  [block]${kw ? `  token: ${kw.token}  category: ${kw.category}` : ''}`];
    lines.push(`  ${overlay.blocks[name]}`);
    lines.push(`  items (${b.items.length}): ${b.items.join(' ')}`);
    const nested = Object.entries(b.nested || {}).sort(([a], [c]) => a.localeCompare(c));
    if (nested.length) {
        lines.push('  nested:');
        for (const [parent, items] of nested) lines.push(`    ${parent} { ${items.join(' ')} }`);
    }
    const snip = loadDescriptions().snippets[name];
    if (snip) lines.push(`  snippet: ${snip.code}   [${snip.from}]`);
    lines.push(`  items from: ${b.sources.join('; ')}`);
    return lines.join('\n');
}

function itemCard(word) {
    const kw = kwByWord.get(word);
    const desc = descriptionOf(word);
    const lines = [`${word}  [item]${kw ? `  token: ${kw.token}  category: ${kw.category}` : ''}`];
    if (desc) lines.push(`  ${desc.text}`);
    const inBlocks = Object.keys(blocks).filter((n) => blocks[n].items.includes(word)).sort();
    if (inBlocks.length) lines.push(`  in blocks (${inBlocks.length}): ${inBlocks.join(', ')}`);
    const nestedIn = new Map();
    for (const [n, b] of Object.entries(blocks))
        for (const [parent, items] of Object.entries(b.nested || {}))
            if (items.includes(word)) {
                if (!nestedIn.has(parent)) nestedIn.set(parent, []);
                nestedIn.get(parent).push(n);
            }
    const nestedRows = [...nestedIn.entries()].sort(([a], [c]) => a.localeCompare(c))
        .map(([parent, names]) => `    ${parent} (${names.length === 1 ? names[0] : `${names.length} blocks`})`);
    if (nestedRows.length) {
        lines.push('  nested in:');
        lines.push(...nestedRows);
    }
    const snip = loadDescriptions().snippets[word];
    if (snip) lines.push(`  snippet: ${snip.code}   [${snip.from}]`);
    if (desc) lines.push(`  ${desc.provenance}`);
    return lines.join('\n');
}

function overlayCard(word) {
    const w = overlay.words[word];
    const kw = kwByWord.get(word);
    const lines = [`${word}  [${w.kind}]${kw ? `  token: ${kw.token}  category: ${kw.category}` : '  4.0'}`];
    lines.push(`  ${w.text}`);
    const snip = loadDescriptions().snippets[word];
    if (snip) lines.push(`  snippet: ${snip.code}   [${snip.from}]`);
    return lines.join('\n');
}

export function cardFor(term) {
    if (blocks[term]) return blockCard(term);
    if (overlay.words[term]) return overlayCard(term);
    const universe = itemUniverse();
    if (universe.has(term)) return itemCard(term);
    return null;
}

export function allTerms() {
    return [...Object.keys(blocks), ...Object.keys(overlay.words), ...itemUniverse()].sort();
}

// Mirrors the checker's rule (extensions/vscode/server/analysis.mjs): edit
// distance within 1 for words under six characters, else within 2.
function editDistance(a, b) {
    let prev = Array.from({ length: b.length + 1 }, (_, j) => j);
    for (let i = 1; i <= a.length; ++i) {
        const row = [i];
        for (let j = 1; j <= b.length; ++j)
            row[j] = Math.min(prev[j] + 1, row[j - 1] + 1, prev[j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
        prev = row;
    }
    return prev[b.length];
}

export function nearMiss(term) {
    const words = [...Object.keys(blocks), ...Object.keys(overlay.words), ...itemUniverse()];
    const scored = words.map((w) => ({ w, d: editDistance(term, w) })).filter(({ w, d }) => d <= (w.length < 6 ? 1 : 2));
    const best = Math.min(...scored.map(({ d }) => d));
    const atBest = scored.filter(({ d }) => d === best).map(({ w }) => w);
    return atBest.length === 1 ? atBest[0] : atBest;
}

export function lookup(term) {
    const card = cardFor(term);
    if (card) return { card };
    const miss = nearMiss(term);
    if (typeof miss === 'string') return { card: cardFor(miss), note: `'${term}' not found; nearest match:` };
    return { candidates: miss };
}

const pad = (s, n) => s + ' '.repeat(Math.max(1, n - s.length));

export function indexTable() {
    const names = [...Object.keys(blocks), ...overlayBlocks()].sort((a, b) => a.localeCompare(b));
    const rows = names.map((n) => `${n} (${blocks[n] ? blocks[n].items.length : 'scene'})`);
    const width = Math.max(...rows.map((r) => r.length)) + 2;
    return rows.map((r, i) => {
        const n = names[i];
        return `${pad(r, width)}${overlay.blocks[n] || overlay.words[n].text}`;
    }).join('\n');
}

export function blockTableMarkdown() {
    const names = [...Object.keys(blocks), ...overlayBlocks()].sort((a, b) => a.localeCompare(b));
    const rows = names.map((n) => `| \`${n}\` | ${blocks[n] ? blocks[n].items.length : '—'} | ${overlay.blocks[n] || overlay.words[n].text} |`);
    return ['| block | items | one-liner |', '|---|---|---|', ...rows].join('\n');
}

// -------------------------------------------------------------------- main

const invokedDirectly = process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href;
if (invokedDirectly) {
    const [arg] = process.argv.slice(2);
    if (arg === '--list') {
        console.log(indexTable());
    } else if (arg === '--generate') {
        const { mined, snippets } = generate();
        console.log(`descriptions.json: ${mined} descriptions, ${snippets} snippets; SKILL.md table refreshed`);
    } else if (!arg || arg.startsWith('-')) {
        console.error('usage: node tools/language/reference.mjs <term> | --list | --generate');
        process.exit(2);
    } else {
        const result = lookup(arg);
        if (result.card) {
            if (result.note) console.log(result.note);
            console.log(result.card);
        } else {
            const hint = result.candidates.length ? `; candidates: ${result.candidates.join(', ')}` : '';
            console.error(`no card for '${arg}'${hint}`);
            process.exit(1);
        }
    }
}
