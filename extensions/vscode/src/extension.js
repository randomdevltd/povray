'use strict';

const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');
const pure = require('./pure');
const blocksDoc = require('../pov4-blocks.json');

const LANGUAGES = ['pov', 'pov4'];
const SELECTOR = LANGUAGES.map((language) => ({ scheme: 'file', language }));
const DIAGNOSTIC_LIMIT = 100;

let diagnostics = null;
const running = new Map();

function activate(context) {
    diagnostics = vscode.languages.createDiagnosticCollection('povray');
    const provider = {
        provideCompletionItems(document, position) {
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
        },
    };
    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider(SELECTOR, provider, '.'),
        vscode.workspace.onDidSaveTextDocument(check),
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (event.affectsConfiguration('povray'))
                for (const doc of vscode.workspace.textDocuments) check(doc);
        }),
        { dispose() { for (const child of running.values()) child.kill(); running.clear(); } },
    );
}

function check(document) {
    if (!SELECTOR.some((sel) => (document.languageId === sel.language) && (document.uri.scheme === 'file')))
        return;
    const config = vscode.workspace.getConfiguration('povray', document.uri);
    const executable = config.get('executablePath');
    const previous = running.get(document.uri.toString());
    if (previous) previous.kill();
    if (!executable) {
        diagnostics.delete(document.uri);
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
        if (!byFile.has(file.fsPath)) byFile.set(file.fsPath, { file, list: [] });
        const line = Math.max(0, record.line - 1);
        const column = Math.max(0, record.column - 1);
        const range = new vscode.Range(line, column, line, column + 1);
        byFile.get(file.fsPath).list.push(new vscode.Diagnostic(range, record.message,
            vscode.DiagnosticSeverity.Error));
    }
    const seen = new Set();
    for (const { file, list } of byFile.values()) {
        if (seen.has(file.fsPath)) continue;
        seen.add(file.fsPath);
        diagnostics.set(file, list.slice(0, DIAGNOSTIC_LIMIT));
    }
    if (!seen.has(document.uri.fsPath)) diagnostics.delete(document.uri);
}

function deactivate() {}

module.exports = { activate, deactivate };
