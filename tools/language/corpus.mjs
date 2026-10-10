#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
// Parses classic scenes and includes with libraries/tree-sitter-pov; reports syntax errors and directive contexts.
import { readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '../..');
const grammarDir = join(root, 'libraries/tree-sitter-pov');
const USAGE = 'usage: corpus.mjs [--quiet] [--allow-errors] [--json FILE] [PATH...]';

function parseArgs(argv) {
  const options = { quiet: false, allowErrors: false, json: null, paths: [] };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--quiet') options.quiet = true;
    else if (arg === '--allow-errors') options.allowErrors = true;
    else if (arg === '--json' && argv[i + 1] && !argv[i + 1].startsWith('-')) options.json = resolve(argv[++i]);
    else if (arg.startsWith('-')) { console.error(USAGE); process.exit(2); }
    else options.paths.push(resolve(arg));
  }
  if (!options.paths.length) options.paths = [join(root, 'distribution'), join(root, 'tests')];
  return options;
}

const options = parseArgs(process.argv.slice(2));
const require = createRequire(join(grammarDir, 'package.json'));
const Parser = require('tree-sitter');
const Pov = require(grammarDir);

const DIRECTIVES = new Set(['declare_directive', 'macro_directive', 'if_directive', 'while_directive',
  'for_directive', 'switch_directive', 'break_directive', 'breakpoint_directive', 'include_directive',
  'version_directive', 'default_directive', 'undef_directive', 'message_directive', 'fopen_directive',
  'fclose_directive', 'read_directive', 'write_directive']);
const TRANSPARENT = new Set(['if_directive', 'elseif_clause', 'else_clause', 'while_directive', 'for_directive',
  'switch_directive', 'case_clause', 'range_clause']);
const ITEM_CONTAINERS = new Set(['block', 'bracket_group', 'default_directive', 'array_initializer',
  'dictionary_expression']);
const FILE_IO = new Set(['fopen_directive', 'fclose_directive', 'read_directive', 'write_directive']);
const VALUE_TYPES = new Set(['number', 'string', 'identifier', 'constant', 'vector', 'parenthesized_expression',
  'tuple', 'call_expression', 'index_expression', 'member_expression', 'binary_expression', 'unary_expression']);
const INDEXABLE = new Set(['identifier', 'index_expression']);
const COMMENTS = new Set(['line_comment', 'block_comment']);
const STATEMENTS = new Set([...DIRECTIVES, 'block', 'tag_filter', ...COMMENTS]);
const MAX_ERRORS = 3;

function* sourceFiles(path) {
  if (statSync(path).isDirectory()) {
    for (const entry of readdirSync(path).sort()) yield* sourceFiles(join(path, entry));
  } else if (/\.(pov|inc|mcr)$/i.test(path)) {
    yield path;
  }
}

const at = (node) => ({ line: node.startPosition.row + 1, column: node.startPosition.column + 1 });
const isMacroCall = (node) =>
  node.type === 'call_expression' && node.childForFieldName('function').type === 'identifier';
const isValue = (node) => VALUE_TYPES.has(node.type) && !isMacroCall(node);
const directiveName = (node) => (['declare_directive', 'if_directive', 'message_directive'].includes(node.type)
  ? node.child(0).type : '#' + node.type.replace('_directive', ''));

function branches(node) {
  switch (node.type) {
    case 'if_directive':
      return [node.childrenForFieldName('consequence'),
        ...node.childrenForFieldName('alternative').map((c) => c.childrenForFieldName('body'))];
    case 'while_directive': case 'for_directive':
      return [node.childrenForFieldName('body')];
    case 'switch_directive':
      return node.namedChildren.filter((c) => c.type.endsWith('_clause')).map((c) => c.childrenForFieldName('body'));
    default:
      return [];
  }
}

const hasElse = (node) => node.childrenForFieldName('alternative').some((c) => c.type === 'else_clause');

function contentItems(items) {
  const out = [];
  for (const item of items) {
    if (item.type === ',' || item.type === ';') continue;
    if (TRANSPARENT.has(item.type)) out.push(...branches(item).flatMap(contentItems));
    else if (!DIRECTIVES.has(item.type)) out.push(item);
  }
  return out;
}

const RVALUES = new Set(['block', 'function_block', 'array_expression', 'dictionary_expression', 'colour_expression']);

function valueCount(items, rvalues = false) {
  let count = 0;
  for (const item of items) {
    if (item.type === ',' || item.type === ';') continue;
    if (item.type === 'if_directive') {
      const counts = branches(item).map((b) => valueCount(b, rvalues));
      if (!hasElse(item)) counts.push(0);
      if (counts.some((c) => c !== counts[0])) return null;
      count += counts[0];
    } else if (TRANSPARENT.has(item.type)) {
      if (branches(item).some((b) => valueCount(b, rvalues) !== 0)) return null;
    } else if (!DIRECTIVES.has(item.type)) {
      if (!isValue(item) && !(rvalues && RVALUES.has(item.type))) return null;
      count++;
    }
  }
  return count;
}

function macroKind(node) {
  const body = node.childrenForFieldName('body');
  const items = contentItems(body);
  if (!items.length) return 'directives';
  if (items.some((n) => n.type === 'keyword' || n.type === 'bracket_group')) return 'items';
  if (valueCount(body) === 1) return 'expression';
  return 'statements';
}

function isExpressionIf(node) {
  const parent = node.parent;
  return node.type === 'if_directive' && !!parent && !TRANSPARENT.has(parent.type) &&
    !ITEM_CONTAINERS.has(parent.type) && !['source_file', 'macro_directive', 'ERROR'].includes(parent.type);
}

function contextOf(node) {
  if (node.hasError) return 'partial';
  let parent = node.parent;
  while (parent && TRANSPARENT.has(parent.type) && !isExpressionIf(parent)) parent = parent.parent;
  if (!parent || parent.type === 'source_file') return 'top-level';
  if (parent.type === 'ERROR') return 'partial';
  if (parent.type === 'macro_directive') return macroKind(parent) === 'items' ? 'macro-fragment' : 'macro-body';
  if (ITEM_CONTAINERS.has(parent.type)) return 'block-item';
  return 'expression';
}

function previousItem(node) {
  let previous = node.previousNamedSibling;
  while (previous && COMMENTS.has(previous.type)) previous = previous.previousNamedSibling;
  if (previous?.type === 'declare_directive' && previous.lastChild.type !== ';') {
    previous = previous.childForFieldName('value');
  }
  return previous;
}

function checkOperatorSplice(node, reasons) {
  const previous = previousItem(node);
  if (!TRANSPARENT.has(node.type) || !previous || !isValue(previous)) return;
  for (const branch of branches(node)) {
    const first = branch.find((n) => !DIRECTIVES.has(n.type));
    if (first?.type === 'unary_expression' && ['+', '-'].includes(first.child(0).type)) {
      reasons.push({ reason: 'operator-splice', ...at(node) });
      return;
    }
  }
}

function checkStatements(items, reasons) {
  for (const item of items) {
    const statement = STATEMENTS.has(item.type) || isMacroCall(item) || item.type === 'identifier';
    if (!statement && !item.hasError && item.type !== 'ERROR') reasons.push({ reason: 'stray-item', ...at(item) });
    for (const branch of branches(item)) checkStatements(branch, reasons);
  }
}

function errorSpan(tree) {
  if (tree.rootNode.type === 'ERROR') return [0, tree.rootNode.endIndex];
  const broken = tree.rootNode.namedChildren.filter((n) => n.hasError);
  return broken.length ? [broken[0].startIndex, broken[broken.length - 1].endIndex] : null;
}

function scan(tree, text, summary) {
  const errors = [];
  const reasons = [];
  let syntax = null;
  const cursor = tree.walk();
  let descend = true;
  for (;;) {
    if (descend) {
      const type = cursor.nodeType;
      if (cursor.nodeIsMissing || (type === 'ERROR' && !cursor.currentNode.children.some((c) => c.hasError))) {
        const where = at(cursor);
        const start = cursor.startIndex;
        const snippet = cursor.nodeIsMissing ? `missing ${type}` : text.slice(start, start + 40);
        if (errors.length < MAX_ERRORS) errors.push(`${where.line}:${where.column} ${snippet.replace(/\s+/g, ' ')}`);
        syntax ??= { reason: 'syntax-error', ...where, count: 0 };
        syntax.count++;
      }
      if (type === 'bracket_group') {
        const node = cursor.currentNode;
        const previous = previousItem(node);
        const name = node.parent?.childForFieldName('name');
        if (previous && INDEXABLE.has(previous.type) && !(name && previous.startIndex === name.startIndex)) {
          reasons.push({ reason: 'ambiguous-index', ...at(node) });
        }
      } else if (type === 'ERROR' && cursor.currentNode.children.some((c) => c.type === '#macro')) {
        reasons.push({ reason: 'partial-macro', ...at(cursor) });
      } else if (DIRECTIVES.has(type)) {
        const node = cursor.currentNode;
        const context = contextOf(node);
        summary.directives[directiveName(node)] ??= {};
        const counts = summary.directives[directiveName(node)];
        counts[context] = (counts[context] ?? 0) + 1;
        summary.contexts[context] = (summary.contexts[context] ?? 0) + 1;
        if (FILE_IO.has(type)) reasons.push({ reason: 'file-io', ...at(node) });
        if (type === 'macro_directive') {
          const kind = macroKind(node);
          summary.macros[kind] = (summary.macros[kind] ?? 0) + 1;
          if (node.hasError) reasons.push({ reason: 'partial-macro', ...at(node) });
        }
        checkOperatorSplice(node, reasons);
        if (context === 'expression' && isExpressionIf(node)) {
          if (!hasElse(node)) reasons.push({ reason: 'expression-if-without-else', ...at(node) });
          else if (branches(node).some((b) => valueCount(b, true) !== 1)) {
            reasons.push({ reason: 'expression-if-not-single-value', ...at(node) });
          }
        } else if (context === 'expression') {
          reasons.push({ reason: 'directive-in-expression', ...at(node) });
        }
      }
    }
    if (descend && cursor.gotoFirstChild()) continue;
    if (cursor.gotoNextSibling()) { descend = true; continue; }
    if (!cursor.gotoParent()) break;
    descend = false;
  }
  const stray = [];
  checkStatements(tree.rootNode.namedChildren, stray);
  const span = errorSpan(tree);
  const lineStarts = span && [0, ...[...text.matchAll(/\n/g)].map((m) => m.index + 1)];
  const offset = (r) => lineStarts[r.line - 1] + r.column - 1;
  reasons.push(...stray.filter((r) => !span || offset(r) < span[0] || offset(r) >= span[1]));
  return { errors, reasons: syntax ? [syntax, ...reasons] : reasons, syntax };
}

const parser = new Parser();
parser.setLanguage(Pov);
const summary = {
  files: 0, syntaxClean: 0, wellFormed: 0, directives: {}, contexts: {}, macros: {}, notWellFormed: [],
};

for (const target of options.paths) {
  for (const file of sourceFiles(target)) {
    const name = relative(root, file);
    const text = readFileSync(file, 'latin1');
    const tree = parser.parse((index) => (index < text.length ? text.slice(index, index + 65536) : null));
    const { errors, reasons, syntax } = scan(tree, text, summary);
    summary.files++;
    if (!syntax) summary.syntaxClean++;
    if (!reasons.length) summary.wellFormed++;
    else summary.notWellFormed.push({ file: name, reasons });
    const kinds = [...new Set(reasons.map((r) => r.reason))].join(' ');
    if (syntax) console.log(`error\t${name}\t${errors.join(' | ')}`);
    else if (reasons.length) console.log(`ill-formed\t${name}\t${kinds}`);
    else if (!options.quiet) console.log(`clean\t${name}`);
  }
}

const share = (n) => (summary.files ? ((100 * n) / summary.files).toFixed(1) : '0');
const clean = summary.syntaxClean;
console.log(`${clean}/${summary.files} files parse without syntax errors (${share(clean)}%); ` +
  `${summary.wellFormed} are well-formed (${share(summary.wellFormed)}%)`);
console.log(`directive contexts: ${JSON.stringify(summary.contexts)}`);
if (options.json) writeFileSync(options.json, JSON.stringify(summary, null, 1) + '\n');
if (summary.syntaxClean < summary.files && !options.allowErrors) process.exitCode = 1;
