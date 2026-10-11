'use strict';
// node --test extensions/vscode/test/extension.test.mjs — activates the real extension
// module against the real server through a stubbed 'vscode' API; no VS Code install needed.

import { test } from 'node:test';
import assert from 'node:assert';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { chmodSync, rmSync, writeFileSync } from 'node:fs';

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

const BROKEN_FN = 'let Radius = 2;\nfn Tint((K) { return K; }\nsphere { <0, 0, 0>, Radius }\n';
const CLEAN_FN = 'let Radius = 2;\nfn Tint(K) { return K; }\nsphere { <0, 0, 0>, Radius }\n';

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

const waitFor = (predicate, what) => new Promise((resolve, reject) => {
    const started = Date.now();
    const timer = setInterval(() => {
        let ok = false;
        try {
            ok = predicate();
        } catch (_) { /* keep polling */ }
        if (ok) {
            clearInterval(timer);
            resolve();
        } else if (Date.now() - started > 5000) {
            clearInterval(timer);
            reject(new Error('timeout waiting for ' + what));
        }
    }, 20);
});

const fire = (name, payload) => stub.workspace._emitters[name].emit('fire', payload);
const listed = (doc) => stub.languages.collection && stub.languages.collection.entries
    .get(doc.uri.toString());

function activateOnce() {
    const subscriptions = [];
    extension.activate({ subscriptions });
    return () => {
        for (const item of subscriptions.splice(0)) item.dispose();
    };
}

test.beforeEach(() => {
    stub.configuration.values = { languageServer: 'auto', executablePath: '', includePath: [],
        declare: {} };
});

test('activation starts the server and serves live diagnostics, outline and definition', async (t) => {
    const dispose = activateOnce();
    t.after(dispose);
    const doc = fakeDocument('scene.pov4', 'pov4', BROKEN_FN);
    stub.workspace.textDocuments.push(doc);
    fire('open', doc);

    await waitFor(() => (listed(doc) || []).length === 1, 'live diagnostic on the broken line');
    assert.equal(listed(doc)[0].source, 'povray-lsp');
    assert.equal(listed(doc)[0].range.start.line, 1);

    doc.text = CLEAN_FN;
    doc.version = 2;
    fire('change', { document: doc });
    await waitFor(() => !stub.languages.collection.entries.has(doc.uri.toString()),
        'diagnostics to clear once repaired');

    const symbols = await stub.registrations.symbols[0].provider.provideDocumentSymbols(doc);
    assert.ok(symbols.some((s) => (s.name === 'Radius') && (s.kind === stub.SymbolKind.Variable)));
    assert.ok(symbols.some((s) => (s.name === 'sphere') && (s.kind === stub.SymbolKind.Struct)));

    const location = await stub.registrations.definitions[0].provider
        .provideDefinition(doc, new stub.Position(2, 22));
    assert.ok(location);
    assert.equal(location.range.start.line, 0);

    const items = await stub.registrations.completion[0].provider
        .provideCompletionItems(doc, new stub.Position(2, 24));
    assert.deepEqual(items, [], 'local provider stands down while the server runs');
});

test('renderer diagnostics on save suppress the server squiggle on the same line', async (t) => {
    const fakeRenderer = join(here, 'fake-renderer.js');
    writeFileSync(fakeRenderer, '#!/usr/bin/env node\n' +
        "console.log('scene.pov4:1:5: Parse Error: deliberate');\n");
    chmodSync(fakeRenderer, 0o755);
    t.after(() => rmSync(fakeRenderer, { force: true }));
    stub.configuration.values.executablePath = fakeRenderer;

    const dispose = activateOnce();
    t.after(dispose);
    const doc = fakeDocument('scene.pov4', 'pov4', 'let R = (;\nsphere { <0, 0, 0>, R }\n');
    fire('open', doc);
    await waitFor(() => (listed(doc) || []).length === 1
        && listed(doc)[0].source === 'povray-lsp', 'server squiggle on line 0');

    fire('save', doc);
    await waitFor(() => (listed(doc) || []).some((d) => (d.message === 'Parse Error: deliberate')),
        'renderer diagnostics after save');
    const merged = listed(doc);
    assert.equal(merged.filter((d) => d.range.start.line === 0).length, 1,
        'one squiggle on the shared line');
});

test('languageServer "off" leaves local completion in charge', async (t) => {
    stub.configuration.values.languageServer = 'off';
    const dispose = activateOnce();
    t.after(dispose);
    const doc = fakeDocument('scene3.pov4', 'pov4', CLEAN_FN);
    const items = await stub.registrations.completion[0].provider
        .provideCompletionItems(doc, new stub.Position(0, 0));
    assert.ok(items.some((i) => (i.label === 'sphere') && (i.detail === 'block')));
});

test('closing a document clears its live diagnostics', async (t) => {
    const dispose = activateOnce();
    t.after(dispose);
    const doc = fakeDocument('scene4.pov4', 'pov4', 'let = ;\n');
    fire('open', doc);
    await waitFor(() => (listed(doc) || []).length === 1, 'server diagnostic');
    fire('close', doc);
    await waitFor(() => !stub.languages.collection.entries.has(doc.uri.toString()),
        'diagnostics cleared on close');
});
