// SPDX-License-Identifier: AGPL-3.0-or-later
import { test } from 'node:test';
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { cardFor, allTerms } from '../../reference.mjs';

const here = dirname(dirname(dirname(fileURLToPath(import.meta.url))));
const cli = join(here, 'reference.mjs');
const run = (args) => execFileSync(process.execPath, [cli, ...args], { encoding: 'utf8' });

test('--list covers every block in blocks.json', () => {
    const blocks = JSON.parse(readFileSync(join(here, 'grammar/blocks.json'), 'utf8')).blocks;
    const listed = run(['--list']).split('\n').filter(Boolean).map((l) => l.trim().split(/\s+/)[0]);
    for (const name of Object.keys(blocks)) assert.ok(listed.includes(name), `${name} missing from --list`);
    assert.equal(listed.length, Object.keys(blocks).length);
});

test('regeneration is byte-stable', () => {
    const paths = [join(here, 'reference/descriptions.json'), join(here, 'reference/SKILL.md')];
    const before = paths.map((p) => readFileSync(p, 'utf8'));
    execFileSync(process.execPath, [cli, '--generate'], { cwd: here });
    paths.forEach((p, i) => assert.equal(readFileSync(p, 'utf8'), before[i], `${p} regenerated differently`));
});

test('spot cards carry description, structure and snippet', () => {
    const phong = run(['phong']);
    assert.match(phong, /^phong  \[item\]  token: PHONG_TOKEN/);
    assert.match(phong, /^  The phong keyword in the finish statement controls the amount of phong highlighting on the object\.$/m);
    assert.match(phong, /^  in blocks \(1\): finish$/m);
    assert.match(phong, /^  snippet: /m);
    assert.match(phong, /^  doc: r3_6\.html#/m);
    const finish = run(['finish']);
    assert.match(finish, /^finish  \[block\]/);
    assert.match(finish, /^  items \(\d+\): /m);
    assert.match(finish, /^  nested:$/m);
    assert.match(finish, /^    reflection \{ /m);
    assert.match(finish, /^  snippet: /m);
    const letCard = run(['let']);
    assert.match(letCard, /^let  \[statement\]  4\.0$/m);
    assert.match(letCard, /^  Binds a name in the current function scope/m);
    assert.match(letCard, /^  snippet: /m);
});

test('near-miss lookup resolves phongg to phong', () => {
    const out = run(['phongg']);
    assert.match(out, /^'phongg' not found; nearest match:$/m);
    assert.match(out, /^phong  \[item\]/m);
});

test('unknown terms fail with candidates or an error', () => {
    assert.throws(() => run(['zzzzqqq']), (e) => e.status === 1 && /no card for 'zzzzqqq'/.test(e.stderr.toString()));
});

test('every block, item and overlay word renders a card', () => {
    for (const term of allTerms()) {
        const card = cardFor(term);
        assert.ok(card, `${term} has no card`);
        assert.match(card, /^.{1,40}  \[(block|item|statement|function|literal)\]/);
    }
});
