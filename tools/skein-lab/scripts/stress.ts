import { examples } from '../examples/index.ts';
import { area, buildMesh, cross, dot, norm, parts, selfIntersections, sub } from '../src/index.ts';
import type { Shape, Vec3 } from '../src/index.ts';

const grids: [number, number][] = [[160, 160], [160, 400], [240, 600], [320, 800], [480, 1200], [640, 1600]];

function normalDeviation(m: ReturnType<typeof buildMesh>) {
  const acc = new Float64Array(m.positions.length);
  const P = (k: number): Vec3 => [m.positions[3 * k], m.positions[3 * k + 1], m.positions[3 * k + 2]];
  for (let t = 0; t < m.surfaceTris; t++) {
    const [a, b, c] = [0, 1, 2].map((k) => m.indices[3 * t + k]);
    const f = cross(sub(P(b), P(a)), sub(P(c), P(a)));
    for (const k of [a, b, c]) for (let d = 0; d < 3; d++) acc[3 * k + d] += f[d];
  }
  const angles: number[] = [];
  for (let k = 0; k < m.positions.length / 3; k++) {
    const f: Vec3 = [acc[3 * k], acc[3 * k + 1], acc[3 * k + 2]], n: Vec3 = [m.normals[3 * k], m.normals[3 * k + 1], m.normals[3 * k + 2]];
    if (norm(f) === 0 || norm(n) === 0) continue;
    angles.push((Math.acos(Math.max(-1, Math.min(1, dot(f, n) / norm(f) / norm(n)))) * 180) / Math.PI);
  }
  angles.sort((x, y) => x - y);
  return { mean: angles.reduce((x, y) => x + y) / angles.length, p95: angles[Math.floor(0.95 * angles.length)], over30: angles.filter((x) => x > 30).length / angles.length };
}

for (const name of process.argv.slice(2)) {
  const sh = parts(examples[name])[0] as Shape;
  console.log(`${name}: self-x at 40/80/160 = ${[40, 80, 160].map((n) => selfIntersections(sh, n)).join('/')}`);
  for (const [nu, nv] of grids) {
    const t0 = performance.now();
    const m = buildMesh(sh, nu, nv);
    const ms = performance.now() - t0, d = normalDeviation(m);
    console.log(`  ${nu}x${nv}: ${(ms / 1000).toFixed(2)} s, ${m.positions.length / 3} verts, ${m.indices.length / 3} tris, area ${area(m).toFixed(4)}, ` +
      `facet-vs-FD normal mean ${d.mean.toFixed(2)}° p95 ${d.p95.toFixed(2)}° >30° ${(100 * d.over30).toFixed(2)}%, heap ${(process.memoryUsage().heapUsed / 1e6).toFixed(0)} MB`);
  }
}
