import { examples } from '../examples/index.ts';
import { buildMesh, parts, intersectingPairs } from '../src/index.ts';
const [name, k = '0', n = '40'] = process.argv.slice(2);
const sh = parts(examples[name])[+k], N = +n;
const m = buildMesh(sh, N, N, false);
const where = (t: number) => { const i = m.indices[3 * t]; return `u≈${((i % (N + 1)) / N).toFixed(2)} v≈${(Math.floor(i / (N + 1)) / N).toFixed(2)}`; };
for (const [a, b] of intersectingPairs(m).slice(0, 6)) console.log(where(a), '|', where(b));
