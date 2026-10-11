'use strict';

const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');
const pure = require('./pure');
const lsp = require('./lsp');
const blocksDoc = require('../pov4-blocks.json');

const LANGUAGES = ['pov', 'pov4'];
const SELECTOR = LANGUAGES.map((language) => ({ scheme: 'file', language }));
const DIAGNOSTIC_LIMIT = 100;

const SERVER_MODULE = path.join(__dirname, '..', 'server', 'server.mjs');

let diagnostics = null;
const running = new Map();
const rendererDiags = new Map();
const serverDiags = new Map();
let client = null;

function matches(document) {
    return SELECTOR.some((sel) => (document.languageId === sel.language)
        && (document.uri.scheme === 'file'));
}

function activate(context) {
    diagnostics = vscode.languages.createDiagnosticCollection('povray');
    const provider = {
        async provideCompletionItems(document, position) {
            if (client) {
                if (client.state === 'running') return [];
                if (client.state === 'starting') {
                    await Promise.race([client.ready, new Promise((r) => setTimeout(r, 1500))]);
                    if (client.state === 'running') return [];
                }
            }
            return localCompletion(document, position);
        },
    };
    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider(SELECTOR, provider, '.'),
        vscode.languages.registerDocumentSymbolProvider(SELECTOR, {
            async provideDocumentSymbols(document) {
                if (!client || (client.state !== 'running')) return [];
                try {
                    return await client.symbols(document.uri.toString());
                } catch (_) {
                    return [];
                }
            },
        }),
        vscode.languages.registerDefinitionProvider(SELECTOR, {
            async provideDefinition(document, position) {
                if (!client || (client.state !== 'running')) return null;
                try {
                    return lsp.toLocation(document.uri.toString(),
                        await client.definition(document.uri.toString(), position));
                } catch (_) {
                    return null;
                }
            },
        }),
        vscode.workspace.onDidOpenTextDocument((document) => {
            if (client && matches(document)) client.didOpen(document);
        }),
        vscode.workspace.onDidChangeTextDocument((event) => {
            if (client && matches(event.document)) client.didChange(event.document);
        }),
        vscode.workspace.onDidCloseTextDocument((document) => {
            if (client) client.didClose(document);
        }),
        vscode.workspace.onDidSaveTextDocument(check),
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (event.affectsConfiguration('povray.languageServer'))
                configureClient();
            if (event.affectsConfiguration('povray'))
                for (const doc of vscode.workspace.textDocuments) check(doc);
        }),
        { dispose() {
            for (const child of running.values()) child.kill();
            running.clear();
            if (client) client.stop();
        } },
    );
    configureClient();
}

function localCompletion(document, position) {
    const text = document.getText();
    const offset = document.offsetAt(position);
    const before = text.slice(0, offset);
    const items = [];
    if (/\.\s*\w*$/.test(before)) {
        for (const label of pure.COMPONENTS)
            items.push({ label, kind: vscode.CompletionItemKind.Keyword, detail: 'component' });
        return items;
    }
    const ctx = pure.completionContext(text, offset, blocksDoc.blocks);
    const identifiers = pure.collectIdentifiers(text);
    for (const c of pure.completionsFor(ctx, blocksDoc.blocks, identifiers)) {
        const item = new vscode.CompletionItem(c.label, {
            keyword: vscode.CompletionItemKind.Keyword,
            block: vscode.CompletionItemKind.Module,
            snippet: vscode.CompletionItemKind.Snippet,
            identifier: vscode.CompletionItemKind.Variable,
        }[c.kind] || vscode.CompletionItemKind.Keyword);
        item.detail = c.detail;
        if (c.snippet) {
            item.insertText = new vscode.SnippetString(c.snippet);
            item.command = { title: 'Suggest', command: 'editor.action.triggerSuggest' };
        }
        items.push(item);
    }
    return items;
}

function configureClient() {
    const mode = vscode.workspace.getConfiguration('povray').get('languageServer');
    const wanted = (mode !== 'off') && fs.existsSync(SERVER_MODULE);
    if (wanted && (!client || (client.state === 'stopped'))) {
        const started = new lsp.PovLanguageClient(SERVER_MODULE);
        client = started;
        started.onDiagnostics = (uri, list) => {
            serverDiags.set(uri, list);
            refreshDiagnostics(uri);
        };
        started.onExited = () => {
            if (client !== started) return;
            const stale = [...serverDiags.keys()];
            serverDiags.clear();
            for (const uri of stale) refreshDiagnostics(uri);
        };
        started.start();
        for (const doc of vscode.workspace.textDocuments)
            if (matches(doc)) started.didOpen(doc);
    } else if (!wanted && client && (client.state !== 'stopped')) {
        client.stop();
        client = null;
        const stale = [...serverDiags.keys()];
        serverDiags.clear();
        for (const uri of stale) refreshDiagnostics(uri);
    }
}

// The renderer's messages and the server's parse squiggles share one collection; a line the
// renderer reports suppresses the server's diagnostics there so one error never squiggles twice.
function refreshDiagnostics(uri) {
    const rendered = rendererDiags.get(uri) || [];
    const taken = new Set(rendered.map((diagnostic) => diagnostic.range.start.line));
    const live = (serverDiags.get(uri) || []).filter((diagnostic) =>
        !taken.has(diagnostic.range.start.line));
    const merged = rendered.concat(live);
    if (merged.length) diagnostics.set(vscode.Uri.parse(uri), merged.slice(0, DIAGNOSTIC_LIMIT));
    else diagnostics.delete(vscode.Uri.parse(uri));
}

function check(document) {
    if (!matches(document))
        return;
    const config = vscode.workspace.getConfiguration('povray', document.uri);
    const executable = config.get('executablePath');
    const previous = running.get(document.uri.toString());
    if (previous) previous.kill();
    if (!executable) {
        rendererDiags.delete(document.uri.toString());
        refreshDiagnostics(document.uri.toString());
        return;
    }
    const args = ['+I' + path.basename(document.uri.fsPath), '+W16', '+H16', '-d', '-p', '-gp', '+fp16', '-f'];
    for (const dir of config.get('includePath') || [])
        args.push('+L' + dir);
    for (const [name, value] of Object.entries(config.get('declare') || {}))
        args.push(`Declare=${name}=${value}`);
    const child = spawn(executable, args, { cwd: path.dirname(document.uri.fsPath) });
    running.set(document.uri.toString(), child);
    let output = '';
    child.stdout.on('data', (chunk) => (output += chunk));
    child.stderr.on('data', (chunk) => (output += chunk));
    const timer = setTimeout(() => child.kill(), 30000);
    child.on('close', () => {
        clearTimeout(timer);
        if (running.get(document.uri.toString()) === child) running.delete(document.uri.toString());
        report(document, output);
    });
}

function report(document, output) {
    const found = pure.parseDiagnostics(output);
    const byFile = new Map();
    const docDir = path.dirname(document.uri.fsPath);
    const docName = path.basename(document.uri.fsPath);
    for (const record of found) {
        const name = path.basename(record.file);
        if ((name !== docName) && !fs.existsSync(path.resolve(docDir, record.file)))
            continue;
        const file = (name === docName) ? document.uri : vscode.Uri.file(path.resolve(docDir, record.file));
        if (!byFile.has(file.toString())) byFile.set(file.toString(), { file, list: [] });
        const line = Math.max(0, record.line - 1);
        const column = Math.max(0, record.column - 1);
        byFile.get(file.toString()).list.push(new vscode.Diagnostic(
            new vscode.Range(line, column, line, column + 1), record.message,
            vscode.DiagnosticSeverity.Error));
    }
    byFile.set(document.uri.toString(), byFile.get(document.uri.toString()) || { file: document.uri, list: [] });
    for (const [uri, { list }] of byFile) {
        rendererDiags.set(uri, list.slice(0, DIAGNOSTIC_LIMIT));
        refreshDiagnostics(uri);
    }
}

function deactivate() {}

module.exports = { activate, deactivate };
