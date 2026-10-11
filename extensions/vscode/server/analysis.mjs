// Syntactic analysis of one document: outline symbols, a declaration index for
// go-to-definition, the first parse error. Tree-sitter backed, scanner backed without it.

import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..', '..', '..');
const GRAMMAR_DIRS = { classic: 'tree-sitter-pov', pov4: 'tree-sitter-pov4' };

const SYMBOL_KINDS = { variable: 13, function: 12, block: 23 };
const DETAIL_MAX = 120;

function loadParser(dir) {
    if (process.env.POVRAY_LSP_NO_NATIVE) return null;
    // Repository layout first; a packaged extension falls back to its vendored copy.
    for (const root of [join(repo, 'libraries', dir), join(here, 'vendor', dir)]) {
        try {
            const req = createRequire(join(root, 'package.json'));
            const parser = new (req('tree-sitter'))();
            parser.setLanguage(req(join(root)));
            return parser;
        } catch {
            continue;
        }
    }
    return null;
}

export const grammars = {
    classic: loadParser(GRAMMAR_DIRS.classic),
    pov4: loadParser(GRAMMAR_DIRS.pov4),
};

export function languageFor(languageId, uri) {
    if (languageId === 'pov') return 'classic';
    if (languageId === 'pov4') return 'pov4';
    return (/\.(pov4|inc4)(\?|$)/.test(uri || '')) ? 'pov4' : 'classic';
}

const pos = (point) => ({ line: point.row, character: point.column });
const range = (node) => ({ start: pos(node.startPosition), end: pos(node.endPosition) });

function trailingComment(node, text) {
    let i = node.endIndex;
    while ((i < text.length) && ' \t'.includes(text[i])) ++i;
    let comment = null;
    if (text.startsWith('//', i)) {
        comment = text.slice(i, text.indexOf('\n', i) < 0 ? text.length : text.indexOf('\n', i));
    } else if (text.startsWith('/*', i)) {
        const close = text.indexOf('*/', i);
        if (close >= 0) comment = text.slice(i, close + 2);
    }
    if (!comment) return undefined;
    comment = comment.replace(/^[/*\s]+|[/*\s]+$/g, '');
    return (comment.length > DETAIL_MAX) ? comment.slice(0, DETAIL_MAX - 1) + '…' : comment;
}

const NODE_SHAPES = {
    classic: { declare_directive: ['target', 'variable'], macro_directive: ['name', 'function'],
        block: ['name', 'block'] },
    pov4: { let_statement: ['name', 'variable'], global_statement: ['name', 'variable'],
        function_definition: ['name', 'function'], block: ['name', 'block'] },
};

function treeSymbols(node, language, text, decls) {
    const shapes = NODE_SHAPES[language];
    const symbols = [];
    for (const child of node.namedChildren) {
        const shape = shapes[child.type];
        const nameNode = shape && child.childForFieldName(shape[0]);
        if (!nameNode) {
            for (const nested of treeSymbols(child, language, text, decls)) symbols.push(nested);
            continue;
        }
        if (shape[1] !== 'block')
            decls.push({ name: nameNode.text, kind: shape[1], range: range(nameNode) });
        const symbol = {
            name: nameNode.text,
            kind: SYMBOL_KINDS[shape[1]],
            range: range(child),
            selectionRange: range(nameNode),
            children: treeSymbols(child, language, text, decls),
        };
        if (shape[1] === 'block') {
            const detail = trailingComment(child, text);
            if (detail) symbol.detail = detail;
        }
        symbols.push(symbol);
    }
    return symbols;
}

function firstError(root, text) {
    let found = null;
    const visit = (node) => {
        if (found) return;
        if ((node.type === 'ERROR') || node.isMissing) {
            found = node;
            return;
        }
        for (let i = 0; i < node.childCount; ++i) visit(node.child(i));
    };
    visit(root);
    if (!found) return null;
    let start = pos(found.startPosition);
    let end = pos(found.endPosition);
    if ((start.line === end.line) && (start.character === end.character)) {
        const lineStart = text.lastIndexOf('\n', found.startIndex - 1) + 1;
        const lineEnd = (text.indexOf('\n', found.startIndex) < 0) ? text.length
            : text.indexOf('\n', found.startIndex);
        end = { line: start.line, character: Math.min(start.character + 1, lineEnd - lineStart) };
    }
    const message = found.isMissing ? `Missing ${found.type.replace(/'/g, '')}` : 'Syntax error';
    return { range: { start, end }, message };
}

function lineStarts(text) {
    const starts = [0];
    for (let i = text.indexOf('\n'); i >= 0; i = text.indexOf('\n', i + 1)) starts.push(i + 1);
    return starts;
}

function offsetToPosition(starts, offset) {
    let line = 0;
    for (let i = 0; i < starts.length; ++i) {
        if (starts[i] <= offset) line = i;
        else break;
    }
    return { line, character: offset - starts[line] };
}

const SCAN_DECLS = [
    [/\b(?:let|global)\s+([A-Za-z_]\w*)/g, 'variable'],
    [/\bfn\s+([A-Za-z_]\w*)/g, 'function'],
    [/#\s*macro\s+(?:\()?([A-Za-z_]\w*)/g, 'function'],
    [/#\s*(?:declare|local)\s+(?:\()?([A-Za-z_]\w*)/g, 'variable'],
];

// Scanner fallback for runs without a native grammar: declarations by pattern, blocks by the
// word-before-brace rule of the block scanner in src/pure.js.
function scanAnalysis(text) {
    const starts = lineStarts(text);
    const decls = [];
    for (const [pattern, kind] of SCAN_DECLS) {
        let match;
        while ((match = pattern.exec(text)) !== null) {
            const offset = match.index + match[0].length - match[1].length;
            const start = offsetToPosition(starts, offset);
            decls.push({ name: match[1], kind, range: { start, end: { line: start.line,
                character: start.character + match[1].length } } });
        }
    }
    decls.sort((a, b) => (a.range.start.line - b.range.start.line) ||
        (a.range.start.character - b.range.start.character));

    const top = [];
    const stack = [];
    let pending = null;
    let pendingStart = 0;
    for (let i = 0; i < text.length; ++i) {
        const c = text[i];
        if ((c === '/') && (text[i + 1] === '/')) {
            while ((i < text.length) && (text[i] !== '\n')) ++i;
        } else if ((c === '/') && (text[i + 1] === '*')) {
            i += 2;
            while ((i + 1 < text.length) && !((text[i] === '*') && (text[i + 1] === '/'))) ++i;
            ++i;
        } else if (c === '"') {
            ++i;
            while ((i < text.length) && (text[i] !== '"')) i += (text[i] === '\\') ? 2 : 1;
        } else if (c === '{') {
            stack.push({ name: pending, start: pendingStart, children: [], parent: stack.length
                ? stack[stack.length - 1].children : top });
            pending = null;
        } else if (c === '}') {
            const frame = stack.pop();
            pending = null;
            if (frame && frame.name) {
                frame.parent.push({
                    name: frame.name,
                    kind: SYMBOL_KINDS.block,
                    range: { start: offsetToPosition(starts, frame.start),
                        end: offsetToPosition(starts, i + 1) },
                    selectionRange: { start: offsetToPosition(starts, frame.start),
                        end: offsetToPosition(starts, frame.start + frame.name.length) },
                    children: frame.children,
                });
            } else if (frame) {
                for (const nested of frame.children) frame.parent.push(nested);
            }
        } else if (/[A-Za-z_]/.test(c)) {
            pendingStart = i;
            let j = i;
            while ((j < text.length) && /[A-Za-z0-9_]/.test(text[j])) ++j;
            pending = text.slice(i, j);
            i = j - 1;
        } else if (!/\s/.test(c)) {
            pending = null;
        }
    }

    const symbols = [];
    for (const decl of decls) {
        symbols.push({ name: decl.name, kind: SYMBOL_KINDS[decl.kind], range: decl.range,
            selectionRange: decl.range, children: [] });
    }
    for (const nested of top) symbols.push(nested);
    symbols.sort((a, b) => (a.range.start.line - b.range.start.line) ||
        (a.range.start.character - b.range.start.character));
    return { symbols, decls, error: null };
}

export function analyze(text, language) {
    const parser = grammars[language];
    if (!parser) return scanAnalysis(text);
    try {
        const tree = parser.parse(text);
        const decls = [];
        const symbols = treeSymbols(tree.rootNode, language, text, decls);
        return { symbols, decls, error: firstError(tree.rootNode, text) };
    } catch {
        return scanAnalysis(text);
    }
}

export function wordAt(text, position) {
    const starts = lineStarts(text);
    const line = (position.line >= 0) && (position.line < starts.length) ? position.line
        : starts.length - 1;
    const lineEnd = (line + 1 < starts.length) ? starts[line + 1] - 1 : text.length;
    const from = starts[line];
    const lineText = text.slice(from, lineEnd);
    const col = Math.min(Math.max(position.character, 0), lineText.length);
    let s = col;
    let e = col;
    while ((s > 0) && /[A-Za-z0-9_]/.test(lineText[s - 1])) --s;
    while ((e < lineText.length) && /[A-Za-z0-9_]/.test(lineText[e])) ++e;
    if (s === e) return null;
    return { word: lineText.slice(s, e), position: { line, character: s } };
}

// Nearest preceding declaration of the same name: position-ordered across the whole document,
// which approximates the language's scoping well enough for an editor.
export function definitionOf(analysis, text, position) {
    const use = wordAt(text, position);
    if (!use) return null;
    let best = null;
    for (const decl of analysis.decls) {
        if (decl.name !== use.word) continue;
        const after = (decl.range.start.line < use.position.line) ||
            ((decl.range.start.line === use.position.line) &&
                (decl.range.start.character <= use.position.character));
        if (after) best = decl;
    }
    return best ? { range: best.range, kind: best.kind, name: best.name } : null;
}
