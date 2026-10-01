import { buildMesh } from './mesh.ts';
import type { Mesh, Shape } from './mesh.ts';
import { cross, dot, sub } from './vec.ts';
import type { Vec3 } from './vec.ts';

const EPS = 1e-9;

function segmentHitsTriangle(p: Vec3, q: Vec3, a: Vec3, b: Vec3, c: Vec3): boolean {
  const d = sub(q, p), e1 = sub(b, a), e2 = sub(c, a);
  const h = cross(d, e2), det = dot(e1, h);
  if (Math.abs(det) < 1e-18) return false;
  const f = 1 / det, s = sub(p, a);
  const bu = f * dot(s, h);
  if (bu <= EPS || bu >= 1 - EPS) return false;
  const qv = cross(s, e1), bv = f * dot(d, qv);
  if (bv <= EPS || bu + bv >= 1 - EPS) return false;
  const t = f * dot(e2, qv);
  return t > EPS && t < 1 - EPS;
}

function trianglesIntersect(A: Vec3[], B: Vec3[]): boolean {
  for (let k = 0; k < 3; k++) {
    if (segmentHitsTriangle(A[k], A[(k + 1) % 3], B[0], B[1], B[2])) return true;
    if (segmentHitsTriangle(B[k], B[(k + 1) % 3], A[0], A[1], A[2])) return true;
  }
  return false;
}

export function countSelfIntersections(m: Mesh): number {
  const nt = m.indices.length / 3;
  const tri: Vec3[][] = [], box: number[][] = [], idx: number[][] = [];
  for (let t = 0; t < nt; t++) {
    const ks = [0, 1, 2].map((k) => m.indices[3 * t + k]);
    const vs = ks.map((k): Vec3 => [m.positions[3 * k], m.positions[3 * k + 1], m.positions[3 * k + 2]]);
    idx.push(ks);
    tri.push(vs);
    box.push([0, 1, 2].flatMap((a) => [Math.min(...vs.map((v) => v[a])), Math.max(...vs.map((v) => v[a]))]));
  }
  const order = [...Array(nt).keys()].sort((a, b) => box[a][0] - box[b][0]);
  let hits = 0;
  for (let x = 0; x < nt; x++) {
    const i = order[x], bi = box[i];
    for (let y = x + 1; y < nt; y++) {
      const j = order[y], bj = box[j];
      if (bj[0] > bi[1]) break;
      if (bj[2] > bi[3] || bj[3] < bi[2] || bj[4] > bi[5] || bj[5] < bi[4]) continue;
      if (idx[i].some((k) => idx[j].includes(k))) continue;
      if (trianglesIntersect(tri[i], tri[j])) hits++;
    }
  }
  return hits;
}

export const selfIntersections = (sh: Shape, n = 40): number => countSelfIntersections(buildMesh(sh, n, n, false));
