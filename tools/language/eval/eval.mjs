#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-or-later
// Converts the scene corpus to 4.0, renders A (classic), B (4.0 under the classic version) and C (4.0), compares them.
import { execFileSync, spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { root } from '../classic.mjs';
import { Program, writeConversion } from '../codemod/convert.mjs';
import { classifyDiff, compareImages, decodePng, encodePng } from './png.mjs';

const USAGE = `usage:
  eval plan   --out <dir> [--run-id <id>] [--scenes <dir>] [--includes classic|convert] [--size 160x120]
              [--only A,B,C] [--b-args "<options; {version} is the scene's #version>"] [--library <dir>]
              [--classic-id <id> | --povray-classic <bin>] [--bc-id <id> | --povray <bin>] [--force]
  eval render --run <dir> --povray <bin> [--povray-classic <bin>] [--timeout 120] [--jobs 2]
  eval report --run <dir>      (reads <run>/causes.json when present: { "BC"|"AB": { scene: { cause, note } } })
  eval all    <plan options> --povray <bin> [<render options>]`;

const RENDER_ARGS = ['-A', '-D', '+FN', '-V'];
const SEVERITY = ['identical', 'noise-level', 'different'];

function parseOptions(argv) {
  const options = {};
  for (let i = 0; i < argv.length; i++) {
    if (!argv[i].startsWith('--')) throw new Error(USAGE);
    options[argv[i].slice(2)] = i + 1 < argv.length && !argv[i + 1].startsWith('--') ? argv[++i] : true;
  }
  return options;
}

const sha = (...parts) => createHash('sha256').update(parts.join('\0')).digest('hex');
const readJson = (path) => JSON.parse(readFileSync(path, 'utf8'));
const writeJson = (path, value) => { mkdirSync(dirname(path), { recursive: true }); writeFileSync(path, JSON.stringify(value, null, 1) + '\n'); };

function* povFiles(dir) {
  for (const entry of readdirSync(dir).sort()) {
    const path = join(dir, entry);
    if (statSync(path).isDirectory()) yield* povFiles(path);
    else if (/\.pov$/i.test(entry)) yield path;
  }
}

const directoryHashes = new Map();
function hashDirectory(dir) {
  if (!directoryHashes.has(dir)) {
    const hash = createHash('sha256');
    const walk = (d) => {
      for (const entry of readdirSync(d).sort()) {
        const path = join(d, entry);
        if (statSync(path).isDirectory()) walk(path);
        else hash.update(`${relative(dir, path)}\0`).update(readFileSync(path));
      }
    };
    walk(dir);
    directoryHashes.set(dir, hash.digest('hex'));
  }
  return directoryHashes.get(dir);
}

const removeOutputs = (job) => {
  for (const path of [job.status, job.output, job.output.replace(/\.png$/, '.log')]) rmSync(path, { force: true });
};

function gitShort() {
  try { return execFileSync('git', ['rev-parse', '--short=8', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(); } catch { return null; }
}

export function plan(options) {
  const out = resolve(options.out);
  const stamp = new Date().toISOString().replace(/[-:]/g, '').replace('T', '-').slice(0, 15);
  const commit = gitShort();
  const id = options['run-id'] ?? (commit ? `${stamp}-${commit}` : stamp);
  const run = join(out, 'runs', id);
  const scenesDir = resolve(options.scenes ?? join(root, 'distribution/scenes'));
  const [width, height] = (options.size ?? '160x120').split('x').map(Number);
  const only = (options.only ?? 'A,B,C').split(',');
  const library = resolve(options.library ?? join(root, 'distribution/include'));
  const binary = options['povray-classic'] ?? options.povray;
  const classicId = options['classic-id'] ?? (binary ? sha(readFileSync(binary).toString('latin1')) : null);
  const povrayId = options['bc-id'] ?? (options.povray ? sha(readFileSync(options.povray).toString('latin1')) : '');
  const program = new Program({ includes: options.includes ?? 'classic', includePath: [library] });
  const size = [`+W${width}`, `+H${height}`, ...RENDER_ARGS];
  const cache = join(out, 'cache', 'A');
  const scenes = [];
  let reused = 0;
  for (const path of povFiles(scenesDir)) {
    const rel = relative(scenesDir, path);
    const outScene = join(run, 'pov4', rel.replace(/\.pov$/i, '.pov4'));
    let version = null;
    let results;
    let inputs = null;
    try {
      const declared = program.load(path).tree.rootNode.descendantsOfType('version_directive')[0]?.childForFieldName('version');
      version = declared?.type === 'number' ? declared.text : null;
      results = writeConversion(program.convertScene(path, { folder: dirname(outScene) }), path, outScene);
      inputs = [hashDirectory(dirname(path)), ...program.closure(path).units.map((u) => u.text)];
    } catch (error) {
      results = [{ path, output: null, refusals: [{ reason: 'codemod-error', line: 0, column: 0, detail: error.message }] }];
    }
    const converted = results.find((r) => r.path === path);
    const refusals = results.flatMap((r) => r.refusals.map((x) => ({ file: relative(root, r.path), ...x })));
    const libraries = [dirname(path), library];
    const job = (kind, input, extra = []) => {
      const args = [...size, ...extra];
      const converted4 = kind === 'A' ? [] : results.filter((r) => r.output).map((r) => readFileSync(r.output, 'latin1'));
      const binaryId = kind === 'A' ? classicId ?? '' : povrayId;
      return { kind, input, args, libraries, hash: sha(kind, binaryId, rel, ...(inputs ?? [String(Date.now())]), ...converted4, ...args),
        output: join(run, 'img', `${rel}.${kind}.png`), status: join(run, 'img', `${rel}.${kind}.json`) };
    };
    const jobs = { A: job('A', path) };
    if (classicId && inputs) jobs.A.cacheKey = jobs.A.hash;
    if (converted.output) {
      if (options['b-args']) jobs.B = job('B', converted.output, version ? options['b-args'].replaceAll('{version}', version).split(/\s+/) : []);
      jobs.C = job('C', converted.output);
    }
    for (const j of Object.values(jobs)) {
      if (!existsSync(j.status)) continue;
      const status = readJson(j.status);
      if (status.hash !== j.hash || (options.force && ['error', 'timeout'].includes(status.status))) removeOutputs(j);
    }
    const key = jobs.A.cacheKey;
    const cached = key && existsSync(join(cache, `${key}.json`)) ? readJson(join(cache, `${key}.json`)) : null;
    if (cached && !existsSync(jobs.A.status) && (cached.status !== 'ok' || existsSync(join(cache, `${key}.png`))) &&
        !(options.force && ['error', 'timeout'].includes(cached.status))) {
      mkdirSync(dirname(jobs.A.output), { recursive: true });
      if (cached.status === 'ok') copyFileSync(join(cache, `${key}.png`), jobs.A.output);
      writeJson(jobs.A.status, { ...cached, hash: jobs.A.hash });
      reused++;
    }
    scenes.push({ scene: rel, version, converted: !!converted.output, refusals, jobs });
  }
  const result = { id, created: new Date().toISOString(), out, run, scenesDir, size: [width, height], only,
    includes: options.includes ?? 'classic', bArgs: options['b-args'] ?? null, classicId, scenes };
  writeJson(join(run, 'plan.json'), result);
  const pending = pendingJobs(result).length;
  console.log(`${run}: ${scenes.length} scenes, ${scenes.filter((s) => s.converted).length} converted, ` +
    `${reused} A renders reused, ${pending} renders to do`);
  return result;
}

export const pendingJobs = (planned) => planned.scenes.flatMap((s) => Object.values(s.jobs))
  .filter((j) => !existsSync(j.status) && (planned.only ?? ['A', 'B', 'C']).includes(j.kind));

export const commandLine = (job) =>
  [...job.args, `+I${job.input}`, `+O${job.output}`, ...job.libraries.map((l) => `+L${l}`)];

// Last error lines of a POV-Ray log: the "Parse Error" line with the file and line it names.
export function errorMessage(log) {
  const lines = log.split('\n').map((l) => l.trim()).filter(Boolean);
  const at = lines.findIndex((l) => /(Parse|Render|Fatal) Error|^Error/i.test(l));
  return (at < 0 ? lines.slice(-3) : lines.slice(Math.max(0, at - 2), at + 2)).join(' | ').slice(0, 400);
}

function renderOne(job, binary, timeout) {
  return new Promise((done, fail) => {
    mkdirSync(dirname(job.output), { recursive: true });
    const started = Date.now();
    const child = spawn(binary, commandLine(job), { cwd: dirname(job.input) });
    child.on('error', (error) => fail(new Error(`cannot run ${binary}: ${error.message}`)));
    let log = '';
    child.stdout.on('data', (d) => { log += d; });
    child.stderr.on('data', (d) => { log += d; });
    let timedOut = false;
    const timer = setTimeout(() => { timedOut = true; child.kill('SIGKILL'); }, timeout * 1000);
    child.on('close', (code) => {
      clearTimeout(timer);
      writeFileSync(job.output.replace(/\.png$/, '.log'), log);
      const status = timedOut ? 'timeout' : code === 0 && existsSync(job.output) ? 'ok' : 'error';
      writeJson(job.status, { status, seconds: (Date.now() - started) / 1000, message: status === 'error' ? errorMessage(log) : '',
        hash: job.hash });
      done();
    });
  });
}

export async function render(options) {
  const planned = readJson(join(resolve(options.run), 'plan.json'));
  const jobs = pendingJobs(planned);
  const timeout = Number(options.timeout ?? 120);
  const workers = Number(options.jobs ?? 2);
  if (!(workers >= 1) || !(timeout > 0)) throw new Error('--jobs must be at least 1 and --timeout positive');
  if (!options.povray) throw new Error(USAGE);
  const queue = [...jobs];
  const worker = async () => {
    for (let job; (job = queue.shift());) {
      await renderOne(job, job.kind === 'A' ? options['povray-classic'] ?? options.povray : options.povray, timeout);
      process.stdout.write(`${job.kind} ${relative(planned.run, job.output)}: ${readJson(job.status).status}\n`);
    }
  };
  await Promise.all(Array.from({ length: workers }, worker));
}

// One-line guesses at why a 4.0 run differs from the classic one, from the classic scene text.
export function guessCause(text, version) {
  const guesses = [];
  const v = version ? Number(version) : null;
  if (!/assumed_gamma/.test(text)) guesses.push(v && v < 3.7 ? `${version} scene without assumed_gamma` : 'no assumed_gamma');
  if (/\bmedia\s*\{/.test(text) && !/\bhollow\b/.test(text)) guesses.push('media inside a non-hollow object?');
  if (/\bradiosity\s*\{/.test(text)) guesses.push('radiosity defaults');
  if (/\bphotons\s*\{/.test(text)) guesses.push('photon defaults');
  if (/\bsubsurface\s*\{/.test(text)) guesses.push('subsurface defaults');
  if (v && v < 3.8) guesses.push('3.8+ defaults: default ambient 0 instead of 0.1');
  if (v && v < 3.7 && guesses.length < 2) guesses.push(`version ${version} gated defaults`);
  return guesses.join('; ') || 'unknown: compare by eye';
}

export function classify(planned, entry, cache) {
  const status = (kind) => {
    const job = entry.jobs[kind];
    if (job && existsSync(job.status)) return readJson(job.status);
    return job && (planned.only ?? ['A', 'B', 'C']).includes(kind) ? { status: 'pending' } : null;
  };
  const result = { scene: entry.scene, converted: entry.converted, refusals: entry.refusals, pairs: {}, images: {} };
  for (const kind of ['A', 'B', 'C']) {
    const s = status(kind);
    result[kind] = s;
    if (s?.status === 'ok') result.images[kind] = relative(planned.run, entry.jobs[kind].output);
  }
  const a = result.A;
  if (a?.status === 'ok' && entry.jobs.A.cacheKey && existsSync(entry.jobs.A.output) &&
      !existsSync(join(cache, `${entry.jobs.A.cacheKey}.json`))) {
    mkdirSync(cache, { recursive: true });
    copyFileSync(entry.jobs.A.output, join(cache, `${entry.jobs.A.cacheKey}.png`));
    copyFileSync(entry.jobs.A.status, join(cache, `${entry.jobs.A.cacheKey}.json`));
  }
  const fail = (cls, s) => Object.assign(result, { classification: cls, message: s?.message ?? '' });
  if (!a || a.status === 'pending') return fail('pending');
  if (a.status === 'timeout') return fail('timeout', a);
  if (a.status === 'error') return fail('render-error', a);
  if (!entry.converted) return fail('conversion-refused', { message: entry.refusals.map((r) => `${r.file}:${r.line} ${r.reason}`).slice(0, 4).join('; ') });
  if (!result.B && !result.C) return fail('a-only');
  for (const kind of ['B', 'C']) {
    const s = result[kind];
    if (s?.status === 'pending') return fail('pending');
    if (s?.status === 'timeout') return fail('timeout', s);
    if (s?.status === 'error') return fail('eval-error', s);
  }
  const text = readFileSync(join(planned.scenesDir, entry.scene), 'latin1');
  const pairs = result.B ? [['A', 'B'], ...(result.C ? [['B', 'C']] : [])] : result.C ? [['A', 'C']] : [];
  const decoded = {};
  const image = (k) => (decoded[k] ??= decodePng(readFileSync(entry.jobs[k].output)));
  const notes = [];
  for (const [x, y] of pairs) {
    const d = compareImages(image(x), image(y));
    const cls = classifyDiff(d);
    result.pairs[x + y] = { classification: cls, maxAbs: d.maxAbs, meanAbs: d.meanAbs, differing: d.differing, sizeMismatch: !!d.sizeMismatch };
    if (d.heat && !d.identical) {
      const path = join(planned.run, 'img', `${entry.scene}.${x}${y}.diff.png`);
      mkdirSync(dirname(path), { recursive: true });
      writeFileSync(path, encodePng(d.heat));
      result.images[`${x}${y}`] = relative(planned.run, path);
    }
    if (cls === 'identical') continue;
    if (x + y === 'AB') notes.push(/\bradiosity\s*\{/.test(text) ? 'A≠B: radiosity differs between identical runs; check by eye'
      : 'A≠B: conversion or evaluator defect unless B is plainly right');
    else notes.push(`${x}≠${y}: ${x === 'A' ? 'conversion, evaluator or ' : ''}4.0 defaults (${guessCause(text, entry.version)})`);
  }
  const worst = Object.values(result.pairs).map((p) => p.classification)
    .reduce((w, c) => (SEVERITY.indexOf(c) > SEVERITY.indexOf(w) ? c : w), 'identical');
  return Object.assign(result, { classification: pairs.length ? worst : 'pending', message: notes.join(' · ') });
}

const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const STYLE = `:root{--bg:#fff;--fg:#1d1d1f;--muted:#6e6e73;--line:#d2d2d7;--card:#f5f5f7;--bad:#b3261e;--warn:#8a5a00;--ok:#1e6b34}
@media (prefers-color-scheme:dark){:root{--bg:#161617;--fg:#f5f5f7;--muted:#a1a1a6;--line:#3a3a3c;--card:#1f1f21;--bad:#ff8a80;--warn:#ffcc66;--ok:#7bd88f}}
body{background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif;margin:0 auto;max-width:1200px;padding:16px}
table{border-collapse:collapse;width:100%;font-size:13px}td,th{border-bottom:1px solid var(--line);padding:3px 6px;text-align:left}
.different,.eval-error,.render-error,.timeout{color:var(--bad)}.noise-level,.conversion-refused{color:var(--warn)}.identical{color:var(--ok)}
.card{background:var(--card);border-radius:8px;margin:12px 0;padding:10px}.imgs{display:flex;flex-wrap:wrap;gap:8px}
figure{margin:0}figure img{image-rendering:pixelated;width:320px;max-width:100%;display:block}figure.x1 img{width:160px}figcaption,.muted{color:var(--muted);font-size:12px}
a{color:inherit}.scroll{overflow-x:auto}`;

function page(title, body) {
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>${esc(title)}</title><style>${STYLE}</style></head><body>${body}</body></html>\n`;
}

const FLAGGED = (r) => !['identical', 'pending', 'a-only', 'conversion-refused'].includes(r.classification);

function runPage(planned, results, counts, causes = {}) {
  const pairCell = (r, k) => (r.pairs[k] ? `<span class="${r.pairs[k].classification}">${r.pairs[k].classification}</span>` : '');
  const rows = results.map((r, i) => `<tr><td>${FLAGGED(r) ? `<a href="#s${i}">${esc(r.scene)}</a>` : esc(r.scene)}</td>
<td>${r.converted ? 'yes' : 'refused'}</td><td>${esc(r.A?.status)}</td><td>${esc(r.B?.status)}</td><td>${esc(r.C?.status)}</td>
<td>${pairCell(r, 'AB') || pairCell(r, 'AC')}</td><td>${pairCell(r, 'BC')}</td><td class="${r.classification}">${r.classification}</td></tr>`).join('\n');
  const figure = (r, k, caption, cls) => (r.images[k] ? `<figure${cls ? ` class="${cls}"` : ''}><img src="${esc(r.images[k].split('/').map(encodeURIComponent).join('/'))}" alt="${k}"><figcaption>${caption}</figcaption></figure>` : '');
  const metric = (p) => `max ${p.maxAbs ?? '–'}, mean ${p.meanAbs?.toFixed(2) ?? '–'}, ${((p.differing ?? 0) * 100).toFixed(1)}% px`;
  const differs = (r, k) => r.pairs[k] && r.pairs[k].classification !== 'identical';
  const errors = results.filter((r) => ['eval-error', 'render-error', 'timeout'].includes(r.classification));
  const pairCard = (r, k, cause) => `<div class="card"><b>${esc(r.scene)}</b> <span class="${r.pairs[k].classification}">${r.pairs[k].classification}</span>
<div>${esc(cause?.note ?? '')}</div><div class="imgs">${figure(r, k[0], k[0])}${figure(r, k[1], k[1])}</div>
<details><summary class="muted">${k[0]}↔${k[1]} ${metric(r.pairs[k])}</summary><div class="imgs">${k === 'BC' ? '' : figure(r, 'C', 'C', 'x1')}${figure(r, k, 'difference', 'x1')}</div></details></div>`;
  const grouped = (k) => {
    const groups = new Map();
    for (const r of results.filter((x) => differs(x, k))) {
      const cause = causes[k]?.[r.scene]?.cause ?? 'not attributed';
      if (!groups.has(cause)) groups.set(cause, []);
      groups.get(cause).push(r);
    }
    return [...groups].sort((a, b) => b[1].length - a[1].length).map(([cause, rs]) =>
      `<h3>${esc(cause)} (${rs.length})</h3>${rs.map((r) => pairCard(r, k, causes[k]?.[r.scene])).join('')}`).join('') || '<p>None.</p>';
  };
  const cards = Object.keys(causes).length ? `<h2>A ≠ B: classic against 4.0 at the scene's version</h2>${grouped('AB')}
<h2>B ≠ C: 4.0 defaults, by cause</h2><p class="muted">B is the scene at its own version, C the conversion at 4.0. Images at 2×.</p>${grouped('BC')}
<h2>Errors and timeouts (${errors.length})</h2><div class="scroll"><table>${errors.map((r) => `<tr><td>${esc(r.scene)}</td><td class="${r.classification}">${r.classification}</td><td>${esc(r.message)}</td></tr>`).join('\n')}</table></div>`
    : `<h2>Flagged</h2>` + (results.map((r, i) => (!FLAGGED(r) ? '' : `
<div class="card" id="s${i}"><b>${esc(r.scene)}</b> <span class="${r.classification}">${r.classification}</span>
<div class="muted">${esc(r.message)}</div><div class="imgs">
${figure(r, 'A', 'A classic')}${figure(r, 'B', 'B 4.0, classic version')}${figure(r, 'C', 'C 4.0')}
${Object.entries(r.pairs).map(([k, p]) => figure(r, k, `${k[0]}↔${k[1]} ${metric(p)}`)).join('')}</div></div>`)).join('') || '<p>None.</p>');
  const refused = results.filter((r) => r.classification === 'conversion-refused').map((r) =>
    `<tr><td>${esc(r.scene)}</td><td>${esc(r.refusals.map((x) => `${x.file}:${x.line}:${x.column} ${x.reason}${x.detail ? ` (${x.detail})` : ''}`).slice(0, 6).join('; '))}</td></tr>`).join('\n');
  return page(`Language eval ${planned.id}`, `<h1>Language eval ${esc(planned.id)}</h1>
<p class="muted">${esc(planned.created)} · ${planned.scenes.length} scenes · ${planned.size.join('×')} · includes ${esc(planned.includes)}
· B ${planned.bArgs ? esc(planned.bArgs) : 'skipped'} · <a href="../../index.html">all runs</a></p>
<table><tr>${Object.entries(counts).map(([k, n]) => `<th class="${k}">${k}</th>`).join('')}</tr>
<tr>${Object.values(counts).map((n) => `<td>${n}</td>`).join('')}</tr></table>
${cards}
<h2>Refused conversions</h2><div class="scroll"><table>${refused}</table></div>
<h2>All scenes</h2><div class="scroll"><table><tr><th>scene</th><th>converted</th><th>A</th><th>B</th><th>C</th><th>A↔B / A↔C</th><th>B↔C</th><th>class</th></tr>
${rows}</table></div>`);
}

function indexPage(out) {
  const runs = existsSync(join(out, 'runs')) ? readdirSync(join(out, 'runs'))
    .filter((d) => existsSync(join(out, 'runs', d, 'report.json')))
    .map((d) => readJson(join(out, 'runs', d, 'report.json')))
    .sort((a, b) => b.created.localeCompare(a.created)) : [];
  const rows = runs.map((r) => `<tr><td><a href="runs/${esc(encodeURIComponent(r.id))}/index.html">${esc(r.id)}</a></td><td>${esc(r.created)}</td>
<td>${Object.entries(r.counts).map(([k, n]) => `<span class="${k}">${k} ${n}</span>`).join(' · ')}</td></tr>`).join('\n');
  writeFileSync(join(out, 'index.html'), page('Language eval runs', `<h1>Language eval runs</h1><div class="scroll"><table>
<tr><th>run</th><th>created</th><th>classification</th></tr>${rows}</table></div>`));
}

export function report(options) {
  const planned = readJson(join(resolve(options.run), 'plan.json'));
  const cache = join(planned.out, 'cache', 'A');
  const results = planned.scenes.map((entry) => {
    try {
      return classify(planned, entry, cache);
    } catch (error) {
      return { scene: entry.scene, converted: entry.converted, refusals: entry.refusals, pairs: {}, images: {},
        classification: 'eval-error', message: `report: ${error.message}` };
    }
  });
  const counts = {};
  for (const r of results) counts[r.classification] = (counts[r.classification] ?? 0) + 1;
  writeJson(join(planned.run, 'report.json'), { id: planned.id, created: planned.created, counts, scenes: results });
  const causes = existsSync(join(planned.run, 'causes.json')) ? readJson(join(planned.run, 'causes.json')) : {};
  writeFileSync(join(planned.run, 'index.html'), runPage(planned, results, counts, causes));
  indexPage(planned.out);
  console.log(`${join(planned.run, 'index.html')}: ${JSON.stringify(counts)}`);
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const [command, ...rest] = process.argv.slice(2);
  try {
    const options = parseOptions(rest);
    if (command === 'plan') plan(options);
    else if (command === 'render') await render(options);
    else if (command === 'report') report(options);
    else if (command === 'all') {
      const planned = plan(options);
      await render({ ...options, run: planned.run });
      report({ run: planned.run });
    } else throw new Error(USAGE);
  } catch (error) {
    console.error(error.message);
    process.exit(1);
  }
}
