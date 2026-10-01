import { examples } from '../examples/index.ts';
import { buildMesh, parts, intersectingPairs } from '../src/index.ts';
const sh = parts(examples[process.argv[2]])[0];
const m = buildMesh(sh, 40, 40, false);
for (const [a, b] of intersectingPairs(m)) {
  const show = (t: number) => `${t < m.surfaceTris ? 'surf' : 'cap'}#${t} [${[0, 1, 2].map((k) => m.indices[3 * t + k]).join(',')}] ` +
    [0, 1, 2].map((k) => Array.from(m.positions.slice(3 * m.indices[3 * t + k], 3 * m.indices[3 * t + k] + 3)).map((x) => x.toFixed(4)).join(' ')).join(' | ');
  console.log(show(a)); console.log(show(b)); console.log('');
}
