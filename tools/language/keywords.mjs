#!/usr/bin/env node
// Generates keywords.json, the classic reserved words with their expression category, for both grammars.
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const parser = join(here, '../../source/parser');
const header = readFileSync(join(parser, 'reservedwords.h'), 'utf8');
const table = readFileSync(join(parser, 'reservedwords.cpp'), 'utf8');

const enumBody = header.slice(header.indexOf('enum TokenId'), header.indexOf('TOKEN_COUNT_'));
const categoryOf = new Map();
let category = 'signature';
const next = { signature: 'float', float: 'vector', vector: 'colour', colour: 'other' };
for (const [, name] of enumBody.matchAll(/^\s*([A-Z0-9_]+_TOKEN(?:_CATEGORY)?)\b/gm)) {
    if (name.endsWith('_TOKEN_CATEGORY')) { category = next[category]; continue; }
    categoryOf.set(name, category);
}

const words = [];
for (const [, token, word] of table.matchAll(/\{\s*([A-Z0-9_]+_TOKEN),\s*"([a-z_][a-z0-9_]*)"\s*\}/g))
    words.push({ word, token, category: categoryOf.get(token) ?? 'other' });
words.sort((a, b) => (a.word < b.word ? -1 : a.word > b.word ? 1 : 0));

const unique = [...new Map(words.map((w) => [w.word, w])).values()];
writeFileSync(join(here, 'keywords.json'), JSON.stringify(unique, null, 1) + '\n');
console.log(`${unique.length} reserved words`);
