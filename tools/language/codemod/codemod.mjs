#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
// codemod <in>... -o <out-dir> [--includes classic|convert] [-L <dir>]... [--json <report>] [--quiet]
import { readdirSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { root } from '../classic.mjs';
import { Program, writeConversion } from './convert.mjs';

const usage = 'usage: codemod <scene.pov|dir>... -o <out-dir> [--includes classic|convert] [-L <dir>]... [--json <file>] [--quiet]';
const args = process.argv.slice(2);
const inputs = [];
const options = { includes: 'classic', includePath: [], out: null, json: null, quiet: false };
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '-o') options.out = args[++i];
  else if (a === '--includes') options.includes = args[++i];
  else if (a === '-L') options.includePath.push(resolve(args[++i]));
  else if (a === '--json') options.json = args[++i];
  else if (a === '--quiet') options.quiet = true;
  else if (a.startsWith('-')) { console.error(usage); process.exit(1); }
  else inputs.push(a);
}
if (!inputs.length || !options.out || !['classic', 'convert'].includes(options.includes)) {
  console.error(usage);
  process.exit(1);
}

function* scenes(path, base, explicit) {
  if (statSync(path).isDirectory()) {
    for (const entry of readdirSync(path).sort()) yield* scenes(join(path, entry), base, false);
  } else if (explicit || /\.pov$/i.test(path)) {
    yield { path: resolve(path), rel: relative(base, path) };
  }
}

const program = new Program({
  includes: options.includes,
  includePath: [...options.includePath, join(root, 'distribution/include')],
});
const shown = (path) => (path.startsWith(root + '/') ? relative(root, path) : path);
const report = { includes: options.includes, scenes: [] };
let refused = 0;

for (const input of inputs) {
  const base = statSync(input).isDirectory() ? input : dirname(input);
  for (const scene of scenes(input, base, true)) {
    const outScene = join(options.out, scene.rel.replace(/\.pov$/i, '.pov4').replace(/\.(inc|mcr)$/i, '.inc4'));
    let results;
    try {
      results = program.convertScene(scene.path, { folder: dirname(resolve(outScene)) });
    } catch (error) {
      results = [{ path: scene.path, refusals: [{ reason: 'codemod-error', line: 0, column: 0, detail: error.message }], notes: [], text: null }];
    }
    const files = [];
    for (const { path, output, refusals, notes } of writeConversion(results, scene.path, outScene)) {
      files.push({ file: shown(path), output, refusals, notes });
      for (const r of refusals) console.log(`refused\t${shown(path)}:${r.line}:${r.column}\t${r.reason}\t${r.detail ?? ''}`);
      if (!refusals.length && !options.quiet) console.log(`converted\t${shown(path)}\t${output}`);
    }
    const own = files.find((f) => f.file === shown(scene.path));
    if (own.refusals.length) refused++;
    report.scenes.push({ scene: shown(scene.path), output: own.output, files });
  }
}

console.error(`${report.scenes.length - refused}/${report.scenes.length} scenes converted`);
if (options.json) writeFileSync(options.json, JSON.stringify(report, null, 1) + '\n');
process.exit(refused ? 2 : 0);
