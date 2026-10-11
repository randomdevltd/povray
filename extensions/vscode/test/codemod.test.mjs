'use strict';
// node --test extensions/vscode/test/codemod.test.mjs — the Convert Scene to 4.0 command
// against fake codemod scripts, through the same stubbed 'vscode' API as extension.test.mjs.

import { test } from 'node:test';
import assert from 'node:assert';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { chmodSync, existsSync, rmSync, writeFileSync } from 'node:fs';

const here = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const stub = require('./vscode-stub.js');

const Module = require('module');
const originalResolveFilename = Module._resolveFilename;
Module._resolveFilename = function (request, parent, ...rest) {
    if (request === 'vscode') return require.resolve('./vscode-stub.js');
    return originalResolveFilename.call(this, request, parent, ...rest);
};
const extension = require(join(here, '..', 'src', 'extension.js'));

const DEFAULT_COMMAND = 'node "{repo}/tools/language/codemod/codemod.mjs" "{input}" -o "{outdir}" --includes classic';
const SCENE = '#declare R = 2;\nsphere { <0, 0, 0>, R }\n';

const CONVERT_OK = `#!/usr/bin/env node
import { writeFileSync } from 'node:fs';
import { basename, join } from 'node:path';
const args = process.argv.slice(2);
const input = args[0];
const outdir = args[args.indexOf('-o') + 1];
const output = join(outdir, basename(input).replace(/\\.pov$/i, '.pov4'));
writeFileSync(output, 'let R = 2;\\n');
console.log('converted\\t' + input + '\\t' + output);
`;

const CONVERT_REFUSE = `#!/usr/bin/env node
console.log('refused\\tcodemod-scene.pov:2:1\\tclassic-construct\\t#declare');
process.exit(2);
`;

function fakeDocument(name, languageId, text) {
    const uri = stub.Uri.file(join(here, name));
    const document = { uri, languageId, version: 1, text,
        getText: () => document.text,
        offsetAt: (position) => {
            const lines = document.text.split('\n');
            let offset = 0;
            for (let i = 0; (i < position.line) && (i < lines.length); ++i)
                offset += lines[i].length + 1;
            return offset + Math.min(position.character, lines[Math.min(position.line,
                lines.length - 1)].length);
        } };
    return document;
}

function activateOnce() {
    const subscriptions = [];
    extension.activate({ subscriptions });
    return () => {
        for (const item of subscriptions.splice(0)) item.dispose();
    };
}

function configure(values) {
    stub.configuration.values = Object.assign({ languageServer: 'off', executablePath: '',
        includePath: [], declare: {}, repoRoot: '', codemodCommand: DEFAULT_COMMAND }, values);
}

async function convert() {
    const registration = stub.registrations.commands.find((r) => r.id === 'povray.convertToPov4');
    assert.ok(registration, 'command registered');
    return registration.handler();
}

test.beforeEach(() => {
    stub.window.activeTextEditor = null;
    stub.window.messages.length = 0;
    stub.window.shown.length = 0;
    stub.workspace.opened.length = 0;
});

test('a successful codemod run opens the produced 4.0 file beside the scene', async (t) => {
    const fake = join(here, 'fake-codemod-ok.mjs');
    writeFileSync(fake, CONVERT_OK);
    chmodSync(fake, 0o755);
    const output = join(here, 'codemod-scene.pov4');
    t.after(() => {
        rmSync(fake, { force: true });
        rmSync(output, { force: true });
    });
    configure({ repoRoot: here,
        codemodCommand: 'node "{repo}/fake-codemod-ok.mjs" "{input}" -o "{outdir}"' });

    const dispose = activateOnce();
    t.after(dispose);
    stub.window.activeTextEditor = { document: fakeDocument('codemod-scene.pov', 'pov', SCENE) };
    await convert();

    assert.ok(existsSync(output), 'the .pov4 was written next to the scene');
    assert.deepEqual(stub.window.messages, [], 'no warning on success');
    assert.equal(stub.workspace.opened.length, 1);
    assert.equal(stub.workspace.opened[0].fsPath, output);
    assert.equal(stub.window.shown.length, 1);
    assert.equal(stub.window.shown[0].uri.fsPath, output);
    const channel = stub.window.channels.at(-1);
    assert.equal(channel.name, 'POV-Ray');
    assert.ok(channel.lines.some((l) => l.includes('fake-codemod-ok.mjs') && l.includes('codemod-scene.pov')),
        'the rendered command is logged');
    assert.ok(channel.lines.some((l) => l.includes('converted\t')), 'codemod output is logged');
});

test('a refused scene shows a warning with the first refusal line', async (t) => {
    const fake = join(here, 'fake-codemod-refuse.mjs');
    writeFileSync(fake, CONVERT_REFUSE);
    chmodSync(fake, 0o755);
    t.after(() => rmSync(fake, { force: true }));
    configure({ repoRoot: here,
        codemodCommand: 'node "{repo}/fake-codemod-refuse.mjs" "{input}" -o "{outdir}"' });

    const dispose = activateOnce();
    t.after(dispose);
    stub.window.activeTextEditor = { document: fakeDocument('codemod-scene.pov', 'pov', SCENE) };
    await convert();

    assert.ok(!existsSync(join(here, 'codemod-scene.pov4')), 'nothing converted');
    assert.equal(stub.window.shown.length, 0, 'no document shown');
    assert.equal(stub.window.messages.length, 1);
    assert.equal(stub.window.messages[0].kind, 'warning');
    assert.ok(stub.window.messages[0].message.includes('refused\tcodemod-scene.pov:2:1'),
        'the warning carries the refusal line');
    const channel = stub.window.channels.at(-1);
    assert.ok(channel.lines.some((l) => l.includes('classic-construct')),
        'the full log stays in the channel');
});

test('without repoRoot and a default command the user is told to configure repoRoot', async (t) => {
    configure({});
    const dispose = activateOnce();
    t.after(dispose);
    stub.window.activeTextEditor = { document: fakeDocument('codemod-scene.pov', 'pov', SCENE) };
    await convert();

    assert.equal(stub.window.messages.length, 1);
    assert.equal(stub.window.messages[0].kind, 'info');
    assert.ok(stub.window.messages[0].message.includes('povray.repoRoot'));
    assert.equal(stub.workspace.opened.length, 0, 'nothing ran');
    assert.deepEqual(stub.window.channels.at(-1).lines, [], 'nothing logged');
});
