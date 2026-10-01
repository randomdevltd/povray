import { build } from 'esbuild';
import { copyFileSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';

mkdirSync('dist', { recursive: true });
await build({ entryPoints: ['web/main.ts', 'web/gallery.ts'], bundle: true, format: 'esm', target: 'es2022', outdir: 'dist', minify: true, splitting: true });
for (const page of ['index.html', 'gallery.html']) copyFileSync(`web/${page}`, `dist/${page}`);
const sources = Object.fromEntries(
  readdirSync('examples').filter((f) => f !== 'index.ts').map((f) => [f.replace(/\.ts$/, ''), readFileSync(`examples/${f}`, 'utf8')]),
);
writeFileSync('dist/sources.json', JSON.stringify(sources));
