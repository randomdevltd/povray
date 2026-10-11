'use strict';

const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { spawn, exec } = require('child_process');
const pure = require('./pure');
const lsp = require('./lsp');
const blocksDoc = require('../pov4-blocks.json');

const LANGUAGES = ['pov', 'pov4'];
const SELECTOR = LANGUAGES.map((language) => ({ scheme: 'file', language }));
const DIAGNOSTIC_LIMIT = 100;

const SERVER_MODULE = path.join(__dirname, '..', 'server', 'server.mjs');

const CODEMOD_COMMAND_ID = 'povray.convertToPov4';
const DEFAULT_CODEMOD_COMMAND = 'node "{repo}/tools/language/codemod/codemod.mjs" "{input}" -o "{outdir}" --includes classic';
const CODEMOD_TIMEOUT_MS = 120000;

let diagnostics = null;
let channel = null;
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
    channel = vscode.window.createOutputChannel('POV-Ray');
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
        channel,
        vscode.commands.registerCommand(CODEMOD_COMMAND_ID, convertToPov4),
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

function isClassicScene(document) {
    return !!document && (document.languageId === 'pov') && (document.uri.scheme === 'file');
}

function convertedPath(input) {
    return input.replace(/\.pov$/i, '.pov4').replace(/\.inc$/i, '.inc4');
}

function runCodemod(command, cwd) {
    return new Promise((resolve) => {
        exec(command, { cwd, timeout: CODEMOD_TIMEOUT_MS }, (error, stdout, stderr) => {
            resolve({ code: error ? (error.code ?? 1) : 0, stdout: stdout || '', stderr: stderr || '' });
        });
    });
}

function firstRefusal(stdout, stderr) {
    return (stdout + '\n' + stderr).split('\n').find((line) => line.startsWith('refused\t'));
}

// First error-ish lines of renderer/codemod output, for surfacing in the failure message.
function failureSummary(stdout, stderr) {
    return (stdout + '\n' + stderr)
        .split('\n').map((l) => l.trim())
        .filter((l) => /error|refused|cannot|missing|not found|no such file/i.test(l) && !/^povray: cannot open (the|an) /.test(l))
        .slice(0, 2).join(' | ');
}

async function convertToPov4() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || !isClassicScene(editor.document)) return;
    const document = editor.document;
    const input = document.uri.fsPath;
    const outdir = path.dirname(input);
    const config = vscode.workspace.getConfiguration('povray', document.uri);
    const repoRoot = config.get('repoRoot') || '';
    const template = config.get('codemodCommand') || '';
    if (!repoRoot && (template === DEFAULT_CODEMOD_COMMAND)) {
        vscode.window.showInformationMessage('Set "povray.repoRoot" to a POV-Ray checkout (or override "povray.codemodCommand") to convert scenes.');
        return;
    }
    const command = template
        .replaceAll('{repo}', repoRoot)
        .replaceAll('{input}', input)
        .replaceAll('{outdir}', outdir);
    channel.appendLine('$ ' + command);
    const { code, stdout, stderr } = await vscode.window.withProgress(
        { location: vscode.ProgressLocation.Window, title: 'POV-Ray: converting scene to 4.0' },
        () => runCodemod(command, outdir));
    if (stdout) channel.appendLine(stdout.replace(/\n$/, ''));
    if (stderr) channel.appendLine(stderr.replace(/\n$/, ''));
    if (code !== 0) {
        const refusal = firstRefusal(stdout, stderr);
        const summary = failureSummary(stdout, stderr);
        vscode.window.showWarningMessage(refusal
            ? `Scene not converted: ${refusal}`
            : (summary
                ? `Scene not converted: ${summary} — see the POV-Ray output channel.`
                : `Codemod exited with code ${code}; see the POV-Ray output channel for the full log.`));
        return;
    }
    const output = vscode.Uri.file(convertedPath(input));
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(output));
}

function deactivate() {}

module.exports = { activate, deactivate };
