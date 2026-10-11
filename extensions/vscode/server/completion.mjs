// Completion for one document. The block-context and identifier logic is reused from
// ../src/pure.js so the extension's provider and the server always agree.

import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const require = createRequire(import.meta.url);
const here = dirname(fileURLToPath(import.meta.url));
const pure = require(join(here, '..', 'src', 'pure.js'));
const blocks = require(join(here, '..', 'pov4-blocks.json')).blocks;

const KIND_MAP = { keyword: 14, block: 9, snippet: 15, identifier: 6 };

function offsetOf(text, position) {
    const lines = text.split('\n');
    let offset = 0;
    for (let i = 0; (i < position.line) && (i < lines.length); ++i) offset += lines[i].length + 1;
    return offset + Math.min(Math.max(position.character, 0),
        lines[Math.min(position.line, lines.length - 1)].length);
}

export function completionAt(text, position) {
    const offset = offsetOf(text, position);
    const before = text.slice(0, offset);
    const items = [];
    if (/\.\s*\w*$/.test(before)) {
        for (const label of pure.COMPONENTS)
            items.push({ label, kind: KIND_MAP.keyword, detail: 'component' });
        return items;
    }
    const ctx = pure.completionContext(text, offset, blocks);
    const identifiers = pure.collectIdentifiers(text);
    for (const c of pure.completionsFor(ctx, blocks, identifiers)) {
        const item = { label: c.label, kind: KIND_MAP[c.kind] || KIND_MAP.keyword, detail: c.detail };
        if (c.snippet) {
            item.insertText = c.snippet;
            item.insertTextFormat = 2;
        }
        items.push(item);
    }
    return items;
}
