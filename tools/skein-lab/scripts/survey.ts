import { examples } from '../examples/index.ts';
import { measure, parts, buildMesh } from '../src/index.ts';

const only = process.argv.slice(2);
for (const [name, model] of Object.entries(examples)) {
  if (only.length && !only.includes(name)) continue;
  const t0 = performance.now();
  const nan = parts(model).some((sh) => buildMesh(sh, 24, 24, false).positions.some((x) => !Number.isFinite(x)));
  const m = measure(model);
  const vol = m.volume === null ? 'open' : m.volume.toFixed(4);
  console.log(`${name.padEnd(15)} area ${m.area.toFixed(3).padStart(9)} vol ${vol.padStart(9)} seam ${m.seamGap.toExponential(1)} self-x ${String(m.selfIntersections).padStart(4)}${nan ? ' NaN!' : ''} ${(performance.now() - t0).toFixed(0)}ms`);
}
