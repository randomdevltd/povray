'use strict';
// Minimal 'vscode' shim for exercising src/extension.js under node --test: the API
// surface the extension touches, with registrations and diagnostics recorded for assertions.

const { EventEmitter } = require('events');

class Position {
    constructor(line, character) {
        this.line = line;
        this.character = character;
    }
}

class Range {
    constructor(a, b, c, d) {
        this.start = (a instanceof Position) ? a : new Position(a, b);
        this.end = (c instanceof Position) ? c : new Position(c, d);
    }
}

const DiagnosticSeverity = { Error: 0, Warning: 1, Information: 2, Hint: 3 };

const ProgressLocation = { SourceControl: 1, Window: 10, Notification: 15 };

class Diagnostic {
    constructor(range, message, severity) {
        this.range = range;
        this.message = message;
        this.severity = severity;
        this.source = undefined;
    }
}

const CompletionItemKind = { Text: 0, Method: 1, Function: 2, Constructor: 3, Field: 4,
    Variable: 5, Class: 6, Interface: 7, Module: 8, Property: 9, Unit: 10, Value: 11, Enum: 12,
    Keyword: 13, Snippet: 14, Color: 15, File: 16, Reference: 17 };

class CompletionItem {
    constructor(label, kind) {
        this.label = label;
        this.kind = kind;
    }
}

class SnippetString {
    constructor(value) {
        this.value = value;
    }
}

const SymbolKind = { File: 1, Module: 2, Namespace: 3, Package: 4, Class: 5, Method: 6,
    Property: 7, Field: 8, Constructor: 9, Enum: 10, Interface: 11, Function: 12, Variable: 13,
    Constant: 14, Struct: 23 };

class DocumentSymbol {
    constructor(name, detail, kind, range, selectionRange) {
        this.name = name;
        this.detail = detail;
        this.kind = kind;
        this.range = range;
        this.selectionRange = selectionRange;
        this.children = [];
    }
}

class Location {
    constructor(uri, range) {
        this.uri = uri;
        this.range = range;
    }
}

class Uri {
    constructor(value) {
        this.value = value;
    }

    toString() {
        return this.value;
    }

    get fsPath() {
        return decodeURIComponent(this.value.replace(/^file:\/\//, ''));
    }

    get scheme() {
        return 'file';
    }

    static parse(value) {
        return new Uri(value);
    }

    static file(value) {
        return new Uri('file://' + value);
    }
}

class DiagnosticCollection {
    constructor(name) {
        this.name = name;
        this.entries = new Map();
    }

    set(uri, list) {
        this.entries.set(uri.toString(), list);
    }

    delete(uri) {
        this.entries.delete(uri.toString());
    }

    clear() {
        this.entries.clear();
    }

    get(uri) {
        return this.entries.get(uri.toString());
    }
}

const configuration = { values: {} };

const window = {
    activeTextEditor: null,
    channels: [],
    messages: [],
    shown: [],
    createOutputChannel: (name) => {
        const channel = { name, lines: [] };
        channel.appendLine = (line) => { channel.lines.push(String(line)); return channel; };
        channel.clear = () => { channel.lines = []; };
        channel.show = () => {};
        channel.dispose = () => {};
        window.channels.push(channel);
        return channel;
    },
    withProgress: (options, task) => Promise.resolve(task({ report() {} }, { isCancellationRequested: false })),
    showInformationMessage: (message) => {
        window.messages.push({ kind: 'info', message });
        return Promise.resolve(undefined);
    },
    showWarningMessage: (message) => {
        window.messages.push({ kind: 'warning', message });
        return Promise.resolve(undefined);
    },
    showErrorMessage: (message) => {
        window.messages.push({ kind: 'error', message });
        return Promise.resolve(undefined);
    },
    showTextDocument: (document) => {
        window.shown.push(document);
        return Promise.resolve(undefined);
    },
};

const workspace = {
    textDocuments: [],
    opened: [],
    _emitters: {
        open: new EventEmitter(), change: new EventEmitter(), close: new EventEmitter(),
        save: new EventEmitter(), config: new EventEmitter(),
    },
    _register: (name, handler) => {
        workspace._emitters[name].on('fire', handler);
        return { dispose: () => workspace._emitters[name].removeListener('fire', handler) };
    },
    onDidOpenTextDocument: (handler) => workspace._register('open', handler),
    onDidChangeTextDocument: (handler) => workspace._register('change', handler),
    onDidCloseTextDocument: (handler) => workspace._register('close', handler),
    onDidSaveTextDocument: (handler) => workspace._register('save', handler),
    onDidChangeConfiguration: (handler) => workspace._register('config', handler),
    openTextDocument: async (target) => {
        workspace.opened.push(target);
        const uri = (typeof target === 'string') ? Uri.file(target) : target;
        return { uri, languageId: 'pov4', version: 1, text: '', getText: () => '' };
    },
    getConfiguration: () => ({
        get: (key) => configuration.values[key],
    }),
};

const registrations = { completion: [], symbols: [], definitions: [], commands: [] };

const languages = {
    createDiagnosticCollection: (name) => {
        languages.collection = new DiagnosticCollection(name);
        return languages.collection;
    },
    registerCompletionItemProvider: (selector, provider, ...trigger) => {
        registrations.completion.push({ selector, provider, trigger });
        return { dispose() {} };
    },
    registerDocumentSymbolProvider: (selector, provider) => {
        registrations.symbols.push({ selector, provider });
        return { dispose() {} };
    },
    registerDefinitionProvider: (selector, provider) => {
        registrations.definitions.push({ selector, provider });
        return { dispose() {} };
    },
};

const commands = {
    registerCommand: (id, handler) => {
        registrations.commands.push({ id, handler });
        return { dispose() {} };
    },
};

module.exports = { Position, Range, DiagnosticSeverity, Diagnostic, CompletionItem,
    CompletionItemKind, SnippetString, SymbolKind, DocumentSymbol, Location, Uri,
    DiagnosticCollection, ProgressLocation, workspace, window, languages, commands,
    configuration, registrations };
