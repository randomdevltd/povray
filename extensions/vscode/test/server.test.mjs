'use strict';
// node --test extensions/vscode/test/server.test.mjs — spawns the LSP server and speaks LSP
// over stdio, no vscode dependency. Parse assertions skip when no native binding is present.

import { test } from 'node:test';
import assert from 'node:assert';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const SERVER = join(here, '..', 'server', 'server.mjs');
const TIMEOUT_MS = 5000;

const POV4 = [
    'let Radius = 2;',
    'fn Tint(K) { return K; }',
    'global Mode = 1;',
    'camera { location <0, 2, -6> look_at 0 }',
    'sphere { <0, 0, 0>, Radius texture { pigment { rgb Tint(1) } } } // the ball',
].join('\n') + '\n';

const CLASSIC = [
    '#declare Foo = 2;',
    '#local Bar = sphere { <0, 0, 0>, 1 }',
    '#macro Baz(A) sphere { 0, A } #end',
    'camera { location <0, 2, -6> }',
].join('\n') + '\n';

class Client {
    constructor(env) {
        this.child = spawn(process.execPath, [SERVER], { stdio: ['pipe', 'pipe', 'pipe'],
            env: { ...process.env, ...env } });
        this.buffer = Buffer.alloc(0);
        this.pending = new Map();
        this.notifications = [];
        this.seq = 0;
        this.child.stdout.on('data', (chunk) => this.receive(chunk));
        this.exitCode = new Promise((resolve) => this.child.on('exit', resolve));
    }

    receive(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        while (true) {
            const headerEnd = this.buffer.indexOf('\r\n\r\n');
            if (headerEnd < 0) return;
            const header = this.buffer.slice(0, headerEnd).toString('utf8');
            const length = /Content-Length:\s*(\d+)/i.exec(header);
            if (!length) {
                this.buffer = this.buffer.slice(headerEnd + 4);
                continue;
            }
            const total = headerEnd + 4 + Number(length[1]);
            if (this.buffer.length < total) return;
            const message = JSON.parse(this.buffer.slice(headerEnd + 4, total).toString('utf8'));
            this.buffer = this.buffer.slice(total);
            if ((message.id !== undefined) && this.pending.has(message.id)) {
                const { resolve, reject } = this.pending.get(message.id);
                this.pending.delete(message.id);
                if (message.error) reject(new Error(message.error.message));
                else resolve(message.result);
            } else if (message.method) {
                this.notifications.push(message);
            }
        }
    }

    write(message) {
        const body = JSON.stringify(message);
        this.child.stdin.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
    }

    notify(method, params) {
        this.write({ jsonrpc: '2.0', method, params });
    }

    request(method, params) {
        const id = ++this.seq;
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            setTimeout(() => {
                if (this.pending.delete(id)) reject(new Error(`timeout: ${method}`));
            }, TIMEOUT_MS);
            this.write({ jsonrpc: '2.0', id, method, params });
        });
    }

    async initialize() {
        const result = await this.request('initialize', {});
        this.notify('initialized', {});
        return result;
    }

    diagnostics(uri) {
        const fresh = this.notifications.filter((n) => (n.method === 'textDocument/publishDiagnostics')
            && (n.params.uri === uri) && !n.consumed);
        if (fresh.length) {
            fresh[fresh.length - 1].consumed = true;
            return Promise.resolve(fresh[fresh.length - 1].params.diagnostics);
        }
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error('timeout: publishDiagnostics')), TIMEOUT_MS);
            const poll = setInterval(() => {
                const next = this.notifications.filter((n) =>
                    (n.method === 'textDocument/publishDiagnostics') && (n.params.uri === uri) && !n.consumed);
                if (next.length) {
                    next[next.length - 1].consumed = true;
                    clearInterval(poll);
                    clearTimeout(timer);
                    resolve(next[next.length - 1].params.diagnostics);
                }
            }, 15);
        });
    }

    async stop() {
        await this.request('shutdown', null);
        this.notify('exit');
        const code = await Promise.race([this.exitCode,
            new Promise((resolve) => setTimeout(() => resolve('timeout'), TIMEOUT_MS))]);
        this.child.kill();
        return code;
    }
}

async function withServer(run, env = {}) {
    const client = new Client(env);
    try {
        await run(client, await client.initialize());
    } finally {
        await client.stop().catch(() => {});
    }
}

test('initialize handshake, capabilities, clean shutdown', async () => {
    await withServer(async (client, init) => {
        assert.equal(init.capabilities.textDocumentSync, 1);
        assert.equal(init.capabilities.documentSymbolProvider, true);
        assert.equal(init.capabilities.definitionProvider, true);
        assert.deepEqual(init.capabilities.completionProvider.triggerCharacters, ['.']);
        assert.equal(init.serverInfo.name, 'povray-lsp');
        assert.ok(Array.isArray(init.serverInfo.grammars));
    });
    const client = new Client();
    await client.initialize();
    assert.equal(await client.stop(), 0);
});

test('documentSymbol outlines a 4.0 scene with declarations and blocks', async () => {
    await withServer(async (client) => {
        const uri = 'file:///scene.pov4';
        client.notify('textDocument/didOpen',
            { textDocument: { uri, languageId: 'pov4', version: 1, text: POV4 } });
        await client.diagnostics(uri);
        const symbols = await client.request('textDocument/documentSymbol', { textDocument: { uri } });
        const byName = new Map(symbols.map((s) => [s.name, s]));
        assert.deepEqual([...byName.keys()], ['Radius', 'Tint', 'Mode', 'camera', 'sphere']);
        assert.equal(byName.get('Radius').kind, 13);
        assert.equal(byName.get('Tint').kind, 12);
        assert.equal(byName.get('sphere').kind, 23);
        assert.equal(byName.get('sphere').detail, 'the ball');
        assert.equal(byName.get('sphere').selectionRange.start.character, 0);
        const texture = byName.get('sphere').children.find((s) => s.name === 'texture');
        assert.ok(texture, 'texture nested under sphere');
        assert.ok(texture.children.some((s) => s.name === 'pigment'));
    });
});

test('documentSymbol outlines a classic scene', async () => {
    await withServer(async (client) => {
        const uri = 'file:///scene.pov';
        client.notify('textDocument/didOpen',
            { textDocument: { uri, languageId: 'pov', version: 1, text: CLASSIC } });
        await client.diagnostics(uri);
        const symbols = await client.request('textDocument/documentSymbol', { textDocument: { uri } });
        const byName = new Map(symbols.map((s) => [s.name, s]));
        assert.deepEqual([...byName.keys()], ['Foo', 'Bar', 'Baz', 'camera']);
        assert.equal(byName.get('Foo').kind, 13);
        assert.equal(byName.get('Baz').kind, 12);
        assert.equal(byName.get('camera').kind, 23);
        assert.ok(byName.get('Bar').children.some((s) => s.name === 'sphere'));
    });
});

test('definition resolves uses to the nearest preceding declaration', async () => {
    await withServer(async (client) => {
        const uri = 'file:///def.pov4';
        const text = [
            'let R = 1;',
            'sphere { <0, 0, 0>, R }',
            'let R = 5;',
            'sphere { <0, 0, 0>, R }',
        ].join('\n') + '\n';
        client.notify('textDocument/didOpen',
            { textDocument: { uri, languageId: 'pov4', version: 1, text } });
        await client.diagnostics(uri);
        const before = await client.request('textDocument/definition',
            { textDocument: { uri }, position: { line: 1, character: 20 } });
        assert.deepEqual(before.range, { start: { line: 0, character: 4 }, end: { line: 0, character: 5 } });
        const after = await client.request('textDocument/definition',
            { textDocument: { uri }, position: { line: 3, character: 20 } });
        assert.equal(after.uri, uri);
        assert.deepEqual(after.range, { start: { line: 2, character: 4 }, end: { line: 2, character: 5 } });
        const unknown = await client.request('textDocument/definition',
            { textDocument: { uri }, position: { line: 1, character: 3 } });
        assert.equal(unknown, null);
    });
});

test('definition resolves classic #declare uses', async () => {
    await withServer(async (client) => {
        const uri = 'file:///def.pov';
        const text = '#declare Size = 2;\nsphere { <0, 0, 0>, Size }\n';
        client.notify('textDocument/didOpen',
            { textDocument: { uri, languageId: 'pov', version: 1, text } });
        await client.diagnostics(uri);
        const found = await client.request('textDocument/definition',
            { textDocument: { uri }, position: { line: 1, character: 20 } });
        assert.deepEqual(found.range, { start: { line: 0, character: 9 }, end: { line: 0, character: 13 } });
    });
});

test('completion offers block items inside, blocks and declarations outside, components after a dot', async () => {
    await withServer(async (client) => {
        const uri = 'file:///comp.pov4';
        const text = 'let Mine = 1;\nfinish { phong 0.5 }\nlet V = Mine.x;\n';
        client.notify('textDocument/didOpen',
            { textDocument: { uri, languageId: 'pov4', version: 1, text } });
        await client.diagnostics(uri);
        const inside = await client.request('textDocument/completion',
            { textDocument: { uri }, position: { line: 1, character: 9 } });
        assert.ok(inside.some((i) => (i.label === 'reflection') && (i.detail === 'finish item')));
        const outside = await client.request('textDocument/completion',
            { textDocument: { uri }, position: { line: 2, character: 0 } });
        assert.ok(outside.some((i) => (i.label === 'sphere') && (i.detail === 'block')));
        assert.ok(outside.some((i) => (i.label === 'Mine') && (i.detail === 'declared in this file')));
        assert.ok(outside.some((i) => (i.label === 'sphere') && (i.insertTextFormat === 2)));
        const dotted = await client.request('textDocument/completion',
            { textDocument: { uri }, position: { line: 2, character: 14 } });
        assert.ok(dotted.some((i) => (i.label === 'red') && (i.detail === 'component')));
        assert.ok(dotted.every((i) => i.detail === 'component'));
    });
});

test('live diagnostics report the first parse error and clear when fixed', async (t) => {
    const probeClient = new Client();
    const probe = await probeClient.initialize();
    const has = (name) => probe.serverInfo.grammars.includes(name);
    probeClient.child.kill();
    if (!has('pov4') || !has('classic'))
        return t.skip('native grammar bindings not available');
    await withServer(async (client) => {
        const uri = 'file:///broken.pov4';
        client.notify('textDocument/didOpen', { textDocument: { uri, languageId: 'pov4',
            version: 1, text: 'let = 1;\n' } });
        let found = await client.diagnostics(uri);
        assert.equal(found.length, 1);
        assert.equal(found[0].severity, 1);
        assert.equal(found[0].range.start.line, 0);
        assert.equal(found[0].source, 'povray-lsp');

        client.notify('textDocument/didChange', { textDocument: { uri, version: 2 },
            contentChanges: [{ text: POV4 }] });
        found = await client.diagnostics(uri);
        assert.equal(found.length, 0);

        const classicUri = 'file:///broken.pov';
        client.notify('textDocument/didOpen', { textDocument: { uri: classicUri, languageId: 'pov',
            version: 1, text: 'camera { location <0, 2, -6>\n' } });
        found = await client.diagnostics(classicUri);
        assert.equal(found.length, 1);
        assert.equal(found[0].range.start.line, 0);

        client.notify('textDocument/didClose', { textDocument: { uri: classicUri } });
        found = await client.diagnostics(classicUri);
        assert.equal(found.length, 0);
    });
});

test('without native grammars the server still serves completion and outline', async () => {
    await withServer(async (client, init) => {
        assert.deepEqual(init.serverInfo.grammars, []);
        const uri = 'file:///degraded.pov4';
        client.notify('textDocument/didOpen', { textDocument: { uri, languageId: 'pov4',
            version: 1, text: 'let Mine = 1;\nfinish { phong 0.5 }\nlet = ;\n' } });
        const diags = await client.diagnostics(uri);
        assert.equal(diags.length, 0);
        const symbols = await client.request('textDocument/documentSymbol', { textDocument: { uri } });
        assert.ok(symbols.some((s) => (s.name === 'Mine') && (s.kind === 13)));
        assert.ok(symbols.some((s) => (s.name === 'finish') && (s.kind === 23)));
        const inside = await client.request('textDocument/completion',
            { textDocument: { uri }, position: { line: 1, character: 9 } });
        assert.ok(inside.some((i) => (i.label === 'phong') && (i.detail === 'finish item')));
        const found = await client.request('textDocument/definition',
            { textDocument: { uri }, position: { line: 1, character: 9 } });
        assert.equal(found, null);
    }, { POVRAY_LSP_NO_NATIVE: '1' });
});
