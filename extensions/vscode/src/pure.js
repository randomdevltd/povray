'use strict';
// Pure helpers shared by the extension and its tests: completion context, snippets, diagnostics parsing.

const DIAG = /([^\s:][^:]*?):(\d+):(\d+): (.*)$/;
const CLASSIC = /['"]?([^\s'":]+\.(?:pov|inc|pov4|inc4))['"]?\s+line\s+(\d+)/i;
const COMPONENTS = ['x', 'y', 'z', 't', 'u', 'v', 'red', 'green', 'blue', 'filter', 'transmit'];

// Parses renderer output into {file, line, column, message} records; line/column are 1-based.
// Continuation lines (wrapped by the renderer) fold into the previous record's message.
function parseDiagnostics(text) {
    const found = [];
    const seen = new Set();
    for (const rawLine of text.split(/\r?\n/)) {
        let match = DIAG.exec(rawLine);
        let record = null;
        if (match)
            record = { file: match[1], line: +match[2], column: +match[3], message: match[4] };
        else if ((match = CLASSIC.exec(rawLine)))
            record = { file: match[1], line: +match[2], column: 1, message: rawLine.trim() };
        if (record && record.line > 0 && !seen.has(JSON.stringify(record))) {
            seen.add(JSON.stringify(record));
            found.push(record);
        } else if (!record && /^ [^ ]/.test(rawLine) && (found.length > 0) &&
                   !/^\s*$/.test(rawLine) && !DIAG.test(rawLine) && !CLASSIC.test(rawLine)) {
            found[found.length - 1].message += ' ' + rawLine.trim();
        }
    }
    return found;
}

// Scans [0, offset) tracking block structure; skips comments and strings.
// Returns the chain of block names enclosing the cursor, innermost last. Anonymous braces push null.
function enclosingBlocks(text, offset, knownBlocks) {
    const known = knownBlocks || new Set();
    const chain = [];
    let pending = null;
    for (let i = 0; i < offset; ++i) {
        const c = text[i];
        if ((c === '/') && (text[i + 1] === '/')) {
            while ((i < offset) && (text[i] !== '\n')) ++i;
        } else if ((c === '/') && (text[i + 1] === '*')) {
            i += 2;
            while ((i + 1 < offset) && !((text[i] === '*') && (text[i + 1] === '/'))) ++i;
            ++i;
        } else if (c === '"') {
            ++i;
            while ((i < offset) && (text[i] !== '"')) i += (text[i] === '\\') ? 2 : 1;
        } else if (c === '{') {
            chain.push(pending);
            pending = null;
        } else if (c === '}') {
            chain.pop();
            pending = null;
        } else if (/[A-Za-z_]/.test(c)) {
            let j = i;
            while ((j < offset) && /[A-Za-z0-9_]/.test(text[j])) ++j;
            pending = text.slice(i, j);
            i = j - 1;
        } else if (!/\s/.test(c)) {
            pending = null;
        }
    }
    return chain;
}

// Innermost schema-known block for the cursor; nested sub-blocks (e.g. reflection inside finish) resolve
// against their parent's nested table.
function completionContext(text, offset, blocks) {
    const chain = enclosingBlocks(text, offset, new Set(Object.keys(blocks)));
    for (let i = chain.length - 1; i >= 0; --i) {
        const name = chain[i];
        if (name && blocks[name])
            return { block: name, items: blocks[name].items };
        if (name) {
            for (let j = i - 1; j >= 0; --j) {
                const parent = chain[j];
                const nested = parent && blocks[parent] && blocks[parent].nested;
                if (nested && nested[name])
                    return { block: name, items: nested[name] };
            }
        }
    }
    return { block: null, items: [] };
}

// Names a user could complete from declarations in this document: 4.0 lets/fns/globals and classic declares.
function collectIdentifiers(text) {
    const names = new Set();
    const patterns = [/\b(?:let|fn|global)\s+([A-Za-z_]\w*)/g, /#\s*(?:declare|local|macro)\s+(?:\()?([A-Za-z_]\w*)/g,
        /^([A-Za-z_]\w*)\s*=/gm];
    for (const re of patterns) {
        let m;
        while ((m = re.exec(text)) !== null)
            names.add(m[1]);
    }
    return [...names];
}

const SNIPPETS = [
    ['sphere', 'sphere { <0, 0, 0>, 1\n\t$0\n}'],
    ['box', 'box { <-1, -1, -1>, <1, 1, 1>$0\n}'],
    ['cylinder', 'cylinder { <0, 0, 0>, <0, 1, 0>, 1$0\n}'],
    ['camera', 'camera {\n\tlocation <0, 2, -6>\n\tlook_at <0, 0, 0>$0\n}'],
    ['light_source', 'light_source {\n\t<10, 20, -20>, rgb 1$0\n}'],
    ['union', 'union {\n\t$0\n}'],
    ['texture', 'texture {\n\tpigment { $1 }\n\tfinish { phong 0.5 }$0\n}'],
    ['finish', 'finish {\n\tambient 0.1 phong 0.5$0\n}'],
    ['function', 'function { $0\n}'],
];

function completionsFor(ctx, blocks, identifiers) {
    const items = [];
    if (ctx.block) {
        for (const word of ctx.items)
            items.push({ label: word, kind: 'keyword', detail: `${ctx.block} item` });
    } else {
        for (const word of Object.keys(blocks))
            items.push({ label: word, kind: 'block', detail: 'block' });
        for (const word of SNIPPETS)
            items.push({ label: word[0], kind: 'snippet', snippet: word[1], detail: 'snippet' });
    }
    for (const name of identifiers || [])
        items.push({ label: name, kind: 'identifier', detail: 'declared in this file' });
    return items;
}

module.exports = { parseDiagnostics, enclosingBlocks, completionContext, collectIdentifiers, completionsFor,
    COMPONENTS, SNIPPETS };
