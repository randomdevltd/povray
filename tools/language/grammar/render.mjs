#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
// Renders doc/language-4-grammar.md: an EBNF-style reference of the 4.0 scene language
// from pov4.json plus the per-block keyword tables from blocks.json.
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const grammar = JSON.parse(readFileSync(join(here, 'pov4.json'), 'utf8'));
const blocks = JSON.parse(readFileSync(join(here, 'blocks.json'), 'utf8')).blocks;
const outPath = join(here, '../../../doc/language-4-grammar.md');

const wrap = (inner, need) => (need ? `(${inner})` : inner);
const leafish = (n) => ['str', 're', 'sym', 'blank', 'field'].includes(n.t);
const renders = (node) => {
    switch (node.t) {
        case 'str':     return `"${node.v}"`;
        case 're':      return `/${node.v}/`;
        case 'sym':     return node.name;
        case 'blank':   return 'ε';
        case 'seq':     return node.of.map((n) => (n.t === 'choice' ? `(${renders(n)})` : renders(n))).join(' ');
        case 'choice':  return node.of.map((n) => renders(n)).join(' | ');
        case 'repeat':  return `${wrap(renders(node.of), !leafish(node.of))}*`;
        case 'repeat1': return `${wrap(renders(node.of), !leafish(node.of))}+`;
        case 'opt':     return `${wrap(renders(node.of), !leafish(node.of))}?`;
        case 'prec':    return wrap(renders(node.of), node.of.t === 'choice');
        case 'field':   return `${node.name}: ${wrap(renders(node.of), node.of.t === 'choice' || node.of.t === 'repeat' || node.of.t === 'seq' && node.of.of.some((k) => k.t === 'choice'))}`;
        case 'token':   return `token(${renders(node.of)}${node.immediate ? ', immediate' : ''})`;
        case 'alias':   return `${renders(node.of, true)} as ${node.name}`;
        default:        return '?';
    }
};

const ruleBlock = (name) => {
    const rule = grammar.rules[name];
    if (!rule) return '';
    const lines = rule.productions.map((p) => renders(p, false));
    return lines.length === 1 ? `${name} := ${lines[0]};\n` : `${name} :=\n${lines.map((l) => `  | ${l}`).join('\n')};\n`;
};

const group = (title, names, note) => `\n### ${title}\n\n${note ? note + '\n\n' : ''}\`\`\`ebnf\n${
    names.filter((n) => grammar.rules[n]).map(ruleBlock).join('\n')}\`\`\`\n`;

const lexicalNames = grammar.groups.lexical;
const otherNames = Object.keys(grammar.rules).filter((n) => !n.startsWith('_') &&
    !grammar.groups.statements.includes(n) && !grammar.groups.expressions.includes(n) &&
    !lexicalNames.includes(n));

const classList = (words) => words.join(' ');
const wc = grammar.wordClasses;
const blockRows = Object.entries(blocks).sort(([a], [b]) => a.localeCompare(b));

let out = `# The 4.0 scene language grammar reference\n
Generated reference for the \`.pov4\` scene language. Regenerate with \`node tools/language/grammar/render.mjs\`;
the sources of truth are the tree-sitter grammar in \`libraries/tree-sitter-pov4\` and the classic
parser's expect-loops (mined into \`tools/language/grammar/blocks.json\`). This file is for reading and
for LLMs; tools should consume \`pov4.json\` and \`blocks.json\` directly.\n
## Lexical words\n
\`identifier\`, \`number\`, \`string\` and \`comment\` are defined in the EBNF below. Reserved words are
classified by the contextual scanner as follows (a word followed by \`(\` that is callable parses as a
built-in call; bare value words parse as identifiers).\n
| class | words |\n|---|---|\n` +
    `| keyword (block and item words) | ${classList(wc.KEYWORD)} |\n` +
    `| value / built-in | ${classList(wc.VALUE)} |\n` +
    `| colour operator | ${classList(wc.COLOUR)} ${classList(wc.COLOR)} |\n` +
    `| colour channel | ${classList(wc.CHANNEL)} |\n` +
    `\n## Syntax\n` +
    group('Statements', grammar.groups.statements) +
    group('Expressions', grammar.groups.expressions) +
    group('Other named rules', otherNames) +
    group('Lexical rules', lexicalNames, 'Hidden rules (leading underscore) are folded into their parents.') +
    `\n## Block keywords\n
Legal reserved-word items inside each block, mined from the classic parser. Sub-blocks such as
\`reflection { ... }\` inside \`finish\` are listed per block. Blocks not listed here are not statically checked.\n
| block | items |\n|---|---|\n` +
    blockRows.map(([word, b]) => `| \`${word}\` | ${b.items.map((i) => `\`${i}\``).join(' ')} |`).join('\n') +
    '\n';

writeFileSync(outPath, out);
console.log(`doc/language-4-grammar.md: ${Object.keys(grammar.rules).length} rules, ${blockRows.length} blocks`);
