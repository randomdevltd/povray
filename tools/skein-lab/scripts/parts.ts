import { examples } from '../examples/index.ts';
import { buildMesh, parts, volume } from '../src/index.ts';
for (const name of process.argv.slice(2))
  console.log(name, parts(examples[name]).map((sh) => volume(buildMesh(sh, 48, 48, false))?.toFixed(4) ?? 'open').join(' '));
