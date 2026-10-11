// SPDX-License-Identifier: AGPL-3.0-or-later
// Bundles the native tree-sitter grammars into server/vendor/ for packaged use; run: node extensions/vscode/scripts/vendor.mjs

import { copyFileSync, cpSync, existsSync, mkdirSync, readdirSync, rmSync } from 'node:fs';
import { dirname, join, relative, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const outRoot = join(repo, 'extensions', 'vscode', 'server', 'vendor');

const GRAMMARS = ['tree-sitter-pov', 'tree-sitter-pov4'];
const MODULES = ['node-gyp-build', 'tree-sitter'];
const MODULE_KEEP = new Set(['package.json', 'index.js', 'node-gyp-build.js', 'optional.js',
    'LICENSE', 'lib', 'build', 'prebuilds']);
const SKIP = [
    /(^|\/)obj\./,
    /\.a$/,
    /(^|\/)binding\.gyp$/,
    /(^|\/)\.?package-lock\.json$/,
    /(^|\/)src\//,
    /(^|\/)grammar\.js$/,
    /(^|\/)queries\//,
    /(^|\/)test(s)?\//,
    /(^|\/)Makefile$/,
    /\.Makefile$/,
    /\.mk$/,
    /(^|\/)config\.gypi$/,
    /(^|\/)\.deps(\/|$)/,
    /\.stamp$/,
    /(^|\/)node-addon-api(\/|$)/,
];

const rel = (root, path) => relative(root, path).split(sep).join('/');
const junk = (path) => SKIP.some((pattern) => pattern.test(path));

function copyTree(from, to, keep) {
    cpSync(from, to, {
        recursive: true,
        dereference: true,
        filter: (path) => keep(rel(from, path)),
    });
}

function fail(message) {
    console.error(`vendor: ${message}`);
    process.exit(1);
}

function vendorGrammar(name) {
    const grammarDir = join(repo, 'libraries', name);
    if (!existsSync(grammarDir)) fail(`missing grammar directory libraries/${name}`);
    const release = join(grammarDir, 'build', 'Release');
    const bindings = existsSync(release) ? readdirSync(release).filter((f) => f.endsWith('.node')) : [];
    if (!bindings.length) {
        fail(`no native binding in libraries/${name}/build/Release — build the native binding first`);
    }

    const out = join(outRoot, name);
    mkdirSync(join(out, 'build', 'Release'), { recursive: true });
    copyFileSync(join(grammarDir, 'package.json'), join(out, 'package.json'));
    copyTree(join(grammarDir, 'bindings'), join(out, 'bindings'), (path) => !junk(path));
    for (const binding of bindings) {
        copyFileSync(join(release, binding), join(out, 'build', 'Release', binding));
    }
    for (const mod of MODULES) {
        const from = join(grammarDir, 'node_modules', mod);
        if (!existsSync(from)) fail(`missing libraries/${name}/node_modules/${mod} — run npm install there`);
        copyTree(from, join(out, 'node_modules', mod), (path) => {
            const head = path.split('/')[0];
            return !path || (MODULE_KEEP.has(head) && !junk(path));
        });
    }
    return bindings;
}

rmSync(outRoot, { recursive: true, force: true });
mkdirSync(outRoot, { recursive: true });
for (const name of GRAMMARS) {
    const bindings = vendorGrammar(name);
    console.log(`${name}: package.json, bindings/, ${bindings.map((b) => `build/Release/${b}`).join(', ')}, ` +
        `node_modules/{${MODULES.join(',')}}`);
}
console.log(`vendored to extensions/vscode/server/vendor/`);
