// Minimal POV-Ray scene-language LSP server over stdio: outline, same-document definitions,
// completion, parse-error squiggles. Missing grammar bindings only remove the squiggles.

import { analyze, definitionOf, grammars, languageFor } from './analysis.mjs';
import { completionAt } from './completion.mjs';

const DEBOUNCE_MS = 200;
const DIAGNOSTIC_SOURCE = 'povray-lsp';

const docs = new Map();
let shutdownRequested = false;

process.stdin.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    while (true) {
        const headerEnd = buffer.indexOf('\r\n\r\n');
        if (headerEnd < 0) return;
        const header = buffer.slice(0, headerEnd).toString('utf8');
        const length = /Content-Length:\s*(\d+)/i.exec(header);
        if (!length) {
            buffer = buffer.slice(headerEnd + 4);
            continue;
        }
        const total = headerEnd + 4 + Number(length[1]);
        if (buffer.length < total) return;
        const body = buffer.slice(headerEnd + 4, total).toString('utf8');
        buffer = buffer.slice(total);
        try {
            handleMessage(JSON.parse(body));
        } catch (error) {
            console.error('povray-lsp:', error);
        }
    }
});
let buffer = Buffer.alloc(0);
process.stdin.on('end', () => process.exit(shutdownRequested ? 0 : 1));

function send(message) {
    const body = JSON.stringify(message);
    process.stdout.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
}

function reply(id, result, error) {
    if (id === undefined) return;
    send({ jsonrpc: '2.0', id, result: error === undefined ? result : undefined, error });
}

function handleMessage(message) {
    const method = message.method;
    if (!method) return;
    if (message.id !== undefined)
        handleRequest(message, method)
            .then(({ result, error }) => reply(message.id, result, error))
            .catch((error) => reply(message.id, undefined,
                { code: -32603, message: String((error && error.message) || error) }));
    else
        handleNotification(method, message.params);
}

async function handleRequest(message, method) {
    if (method === 'initialize') {
        return { result: {
            capabilities: {
                textDocumentSync: 1,
                documentSymbolProvider: true,
                definitionProvider: true,
                completionProvider: { triggerCharacters: ['.'] },
            },
            serverInfo: {
                name: 'povray-lsp',
                version: '0.1.0',
                grammars: Object.entries(grammars).filter(([, parser]) => parser)
                    .map(([name]) => name),
            },
        } };
    }
    if (method === 'shutdown') {
        shutdownRequested = true;
        return { result: null };
    }
    if (!initialized) return { error: { code: -32002, message: 'Server not initialized' } };
    const params = message.params || {};
    const uri = params.textDocument && params.textDocument.uri;
    const doc = docs.get(uri);
    if (method === 'textDocument/documentSymbol') {
        if (!doc) return { result: [] };
        return { result: analysisFor(uri).symbols };
    }
    if (method === 'textDocument/definition') {
        if (!doc) return { result: null };
        const found = definitionOf(analysisFor(uri), doc.text, params.position || {});
        return { result: found ? { uri, range: found.range } : null };
    }
    if (method === 'textDocument/completion') {
        if (!doc) return { result: [] };
        return { result: completionAt(doc.text, params.position || {}) };
    }
    return { error: { code: -32601, message: `Method not found: ${method}` } };
}

let initialized = false;

function handleNotification(method, params) {
    if (method === 'initialized') {
        initialized = true;
        return;
    }
    if (method === 'exit') {
        process.exit(shutdownRequested ? 0 : 1);
        return;
    }
    if (!initialized || !params) return;
    const textDocument = params.textDocument || {};
    const uri = textDocument.uri;
    if (method === 'textDocument/didOpen') {
        docs.set(uri, { text: textDocument.text, language: languageFor(textDocument.languageId, uri),
            cache: null });
        clearTimeout(timers.get(uri));
        timers.delete(uri);
        publish(uri);
    } else if (method === 'textDocument/didChange') {
        const doc = docs.get(uri);
        const change = params.contentChanges && params.contentChanges[params.contentChanges.length - 1];
        if (!doc || (change === undefined)) return;
        doc.text = change.text;
        doc.cache = null;
        clearTimeout(timers.get(uri));
        timers.set(uri, setTimeout(() => {
            timers.delete(uri);
            publish(uri);
        }, DEBOUNCE_MS));
    } else if (method === 'textDocument/didClose') {
        docs.delete(uri);
        clearTimeout(timers.get(uri));
        timers.delete(uri);
        send({ jsonrpc: '2.0', method: 'textDocument/publishDiagnostics',
            params: { uri, diagnostics: [] } });
    }
}

const timers = new Map();

function analysisFor(uri) {
    const doc = docs.get(uri);
    if (!doc.cache || (doc.cache.text !== doc.text))
        doc.cache = { text: doc.text, result: analyze(doc.text, doc.language) };
    return doc.cache.result;
}

function publish(uri) {
    const doc = docs.get(uri);
    const diagnostics = [];
    const result = doc && analysisFor(uri);
    if (result && result.error) {
        diagnostics.push({ range: result.error.range, message: result.error.message,
            severity: 1, source: DIAGNOSTIC_SOURCE });
    }
    send({ jsonrpc: '2.0', method: 'textDocument/publishDiagnostics', params: { uri, diagnostics } });
}
