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
