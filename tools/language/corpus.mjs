#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
// Parses classic scenes and includes with libraries/tree-sitter-pov; reports syntax errors and directive contexts.
import { readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { join, relative, resolve } from 'node:path';
import { parse, root, scan } from './classic.mjs';

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

function* sourceFiles(path) {
  if (statSync(path).isDirectory()) {
    for (const entry of readdirSync(path).sort()) yield* sourceFiles(join(path, entry));
  } else if (/\.(pov|inc|mcr)$/i.test(path)) {
    yield path;
  }
}

const summary = {
  files: 0, syntaxClean: 0, wellFormed: 0, directives: {}, contexts: {}, macros: {}, notWellFormed: [],
};

for (const target of options.paths) {
  for (const file of sourceFiles(target)) {
    const name = relative(root, file);
    const text = readFileSync(file, 'latin1');
    const { errors, reasons, syntax } = scan(parse(text), text, summary);
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
