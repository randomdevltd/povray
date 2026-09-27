// fixtures.mjs <dir>: one wavy sheet as text mesh2, mesh {}, OBJ and a .povm written directly, all with float-exact numbers;
// the OBJ adds a smooth triangle using `vn 0 1 0` and a last face without normals.
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { writePovm } from '../povm.mjs';

const dir = process.argv[2];
const N = 24;
const f = Math.fround;
const s = (x) => String(f(x));
const vec = (a) => `<${a.map(s).join(',')}>`;

const P = [], Nrm = [], UV = [];
for (let j = 0; j <= N; j++) {
  for (let i = 0; i <= N; i++) {
    const x = i / N, z = j / N, y = 0.12 * Math.sin(7 * x + 2 * z) * Math.cos(5 * z);
    const dx = 0.84 * Math.cos(7 * x + 2 * z) * Math.cos(5 * z);
    const dz = 0.24 * Math.cos(7 * x + 2 * z) * Math.cos(5 * z) - 0.6 * Math.sin(7 * x + 2 * z) * Math.sin(5 * z);
    const l = Math.hypot(dx, 1, dz);
    P.push([x, y, z].map(f));
    Nrm.push([-dx / l, 1 / l, -dz / l].map(f));
    UV.push([x, z].map(f));
  }
}
const faces = [];
for (let j = 0; j < N; j++) {
  for (let i = 0; i < N; i++) {
    const a = j * (N + 1) + i, b = a + 1, c = a + N + 1, d = c + 1;
    faces.push([a, c, d], [a, d, b]);
  }
}
const flat = (t) => t % 7 === 3;

writeFileSync(join(dir, 'mesh2.inc'), `#declare M = mesh2 {
  vertex_vectors { ${P.length}, ${P.map(vec).join(', ')} }
  normal_vectors { ${Nrm.length}, ${Nrm.map(vec).join(', ')} }
  uv_vectors { ${UV.length}, ${UV.map(vec).join(', ')} }
  face_indices { ${faces.length}, ${faces.map(vec).join(', ')} }
}\n`);

writeFileSync(join(dir, 'mesh1.inc'), `#declare M = mesh {\n${faces.map((t, k) => {
  const uv = ` uv_vectors ${t.map((v) => vec(UV[v])).join(', ')}`;
  return flat(k)
    ? `  triangle { ${t.map((v) => vec(P[v])).join(', ')}${uv} }`
    : `  smooth_triangle { ${t.map((v) => `${vec(P[v])}, ${vec(Nrm[v])}`).join(', ')}${uv} }`;
}).join('\n')}\n}\n`);

writeFileSync(join(dir, 'mesh.obj'), [
  '# wavy sheet', 'o sheet', 'g top',
  ...P.map((p) => `v ${p.map(s).join(' ')}`),
  ...UV.map((p) => `vt ${p.map(s).join(' ')}`),
  ...Nrm.map((p) => `vn ${p.map(s).join(' ')}`),
  ...faces.map((t) => `f ${t.map((v) => `${v + 1}/${v + 1}/${v + 1}`).join(' ')}`),
  'v 0.35 0.3 0.4', 'v 0.65 0.3 0.4', 'v 0.5 0.35 0.65', 'v 0.5 0.4 0.2',
  'vn 0 1 0', 'vn 0.6 0.8 0', 'vn 0 0.8 0.6',
  `f ${P.length + 1}//${P.length + 1} ${P.length + 2}//${P.length + 2} ${P.length + 3}//${P.length + 3}`,
  `f ${P.length + 1}/1 ${P.length + 4}/1 ${P.length + 2}/1`,
].join('\n') + '\n');

const flatten = (rows, Type) => Type.from(rows.flat());
await writePovm(join(dir, 'writer.povm'), {
  vertices: flatten(P, Float32Array),
  normals: flatten(Nrm, Float32Array),
  normalIndices: flatten(faces, Uint32Array),
  uvs: flatten(UV, Float32Array),
  faces: flatten(faces, Uint32Array),
});
