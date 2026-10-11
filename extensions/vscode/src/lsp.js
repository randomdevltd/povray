'use strict';
// Minimal LSP client for the bundled POV-Ray server: spawn it under node and speak
// JSON-RPC over stdio; keeps the extension packaged without node_modules.

const vscode = require('vscode');
const { spawn } = require('child_process');

const REQUEST_TIMEOUT_MS = 5000;

const SYMBOL_KINDS = new Map([[12, vscode.SymbolKind.Function], [13, vscode.SymbolKind.Variable],
    [23, vscode.SymbolKind.Struct]]);
const COMPLETION_KINDS = new Map([[6, vscode.CompletionItemKind.Variable],
    [9, vscode.CompletionItemKind.Module], [14, vscode.CompletionItemKind.Keyword],
    [15, vscode.CompletionItemKind.Snippet]]);

const rangeOf = (range) =>
    new vscode.Range(range.start.line, range.start.character, range.end.line, range.end.character);

function toSymbol(symbol) {
    const mapped = new vscode.DocumentSymbol(symbol.name, symbol.detail || '',
        SYMBOL_KINDS.get(symbol.kind) || vscode.SymbolKind.Field, rangeOf(symbol.range),
        rangeOf(symbol.selectionRange));
    mapped.children = (symbol.children || []).map(toSymbol);
    return mapped;
}

function toCompletionItem(item) {
    const mapped = new vscode.CompletionItem(item.label, COMPLETION_KINDS.get(item.kind)
        || vscode.CompletionItemKind.Keyword);
    mapped.detail = item.detail;
    if (item.insertTextFormat === 2) {
        mapped.insertText = new vscode.SnippetString(item.insertText);
        mapped.command = { title: 'Suggest', command: 'editor.action.triggerSuggest' };
    } else if (item.insertText) {
        mapped.insertText = item.insertText;
    }
    return mapped;
}

function toDiagnostic(diagnostic) {
    const mapped = new vscode.Diagnostic(rangeOf(diagnostic.range), diagnostic.message,
        vscode.DiagnosticSeverity.Error);
    mapped.source = diagnostic.source;
    return mapped;
}

const toLocation = (uri, location) =>
    (location ? new vscode.Location(vscode.Uri.parse(uri), rangeOf(location.range)) : null);

class PovLanguageClient {
    constructor(serverModule) {
        this.serverModule = serverModule;
        this.state = 'idle';
        this.grammars = [];
        this.pending = new Map();
        this.outbox = [];
        this.seq = 0;
        this.onDiagnostics = null;
        this.onExited = null;
        this.ready = new Promise((resolve, reject) => {
            this.settleReady = (value) => resolve(value);
            this.failReady = (error) => reject(error);
        });
    }

    start() {
        if (this.state !== 'idle') return;
        this.state = 'starting';
        try {
            this.child = spawn(process.execPath, [this.serverModule], { stdio: ['pipe', 'pipe', 'inherit'],
                env: { ...process.env, ELECTRON_RUN_AS_NODE: '1' } });
        } catch (error) {
            this.stopped(error);
            return;
        }
        this.buffer = Buffer.alloc(0);
        this.child.stdout.on('data', (chunk) => this.receive(chunk));
        this.child.on('error', (error) => this.stopped(error));
        this.child.on('exit', () => {
            const wasRunning = (this.state === 'running') || (this.state === 'starting');
            this.stopped(new Error('server exited'));
            if (wasRunning && this.onExited) this.onExited();
        });
        this.request('initialize', { capabilities: {} }).then((result) => {
            this.grammars = (result.serverInfo && result.serverInfo.grammars) || [];
            this.state = 'running';
            this.notify('initialized', {});
            for (const message of this.outbox) this.write(message);
            this.outbox = [];
            this.settleReady(this);
        }, (error) => this.stopped(error));
    }

    stopped(error) {
        if (this.state === 'stopped') return;
        this.state = 'stopped';
        for (const { reject } of this.pending.values()) reject(error);
        this.pending.clear();
        this.failReady(error);
        console.error('povray language server:', error.message);
    }

    async stop() {
        if (!this.child || (this.state === 'stopped')) {
            this.state = 'stopped';
            return;
        }
        this.state = 'stopped';
        try {
            await this.request('shutdown', null);
        } catch (_) { /* the server may already be gone */ }
        this.notify('exit');
        const child = this.child;
        setTimeout(() => child.kill(), 1000).unref();
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
            } else if ((message.method === 'textDocument/publishDiagnostics') && this.onDiagnostics) {
                this.onDiagnostics(message.params.uri,
                    message.params.diagnostics.map(toDiagnostic));
            }
        }
    }

    write(message) {
        if (!this.child || this.child.killed) return;
        const body = JSON.stringify(message);
        this.child.stdin.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
    }

    notify(method, params) {
        const message = { jsonrpc: '2.0', method, params };
        // Notifications that race the handshake would be dropped by the server; hold them.
        if (this.state === 'starting') this.outbox.push(message);
        else this.write(message);
    }

    request(method, params) {
        const id = ++this.seq;
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            setTimeout(() => {
                if (this.pending.delete(id)) reject(new Error(`timeout: ${method}`));
            }, REQUEST_TIMEOUT_MS).unref();
            this.write({ jsonrpc: '2.0', id, method, params });
        });
    }

    didOpen(document) {
        this.notify('textDocument/didOpen', { textDocument: { uri: document.uri.toString(),
            languageId: document.languageId, version: document.version, text: document.getText() } });
    }

    didChange(document) {
        this.notify('textDocument/didChange', { textDocument: { uri: document.uri.toString(),
            version: document.version }, contentChanges: [{ text: document.getText() }] });
    }

    didClose(document) {
        this.notify('textDocument/didClose', { textDocument: { uri: document.uri.toString() } });
    }

    symbols(uri) {
        return this.request('textDocument/documentSymbol', { textDocument: { uri } })
            .then((symbols) => symbols.map(toSymbol));
    }

    definition(uri, position) {
        return this.request('textDocument/definition', { textDocument: { uri },
            position: { line: position.line, character: position.character } });
    }

    completion(uri, position) {
        return this.request('textDocument/completion', { textDocument: { uri },
            position: { line: position.line, character: position.character } })
            .then((items) => items.map(toCompletionItem));
    }
}

module.exports = { PovLanguageClient, toSymbol, toCompletionItem, toDiagnostic, toLocation };
