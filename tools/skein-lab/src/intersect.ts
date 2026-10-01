import { buildMesh, merge, parts } from './mesh.ts';
import type { Mesh, Model } from './mesh.ts';
import { cross, dot, sub } from './vec.ts';
import type { Vec3 } from './vec.ts';

const EPS = -1e-12;

function segmentHitsTriangle(p: Vec3, q: Vec3, a: Vec3, b: Vec3, c: Vec3): boolean {
  const d = sub(q, p), e1 = sub(b, a), e2 = sub(c, a);
  const h = cross(d, e2), det = dot(e1, h);
  if (Math.abs(det) < 1e-18) return false;
  const f = 1 / det, s = sub(p, a);
  const bu = f * dot(s, h);
  if (bu < EPS || bu > 1 - EPS) return false;
  const qv = cross(s, e1), bv = f * dot(d, qv);
  if (bv < EPS || bu + bv > 1 - EPS) return false;
  const t = f * dot(e2, qv);
  return t >= EPS && t <= 1 - EPS;
}

function trianglesIntersect(A: Vec3[], B: Vec3[]): boolean {
  for (let k = 0; k < 3; k++) {
    if (segmentHitsTriangle(A[k], A[(k + 1) % 3], B[0], B[1], B[2])) return true;
    if (segmentHitsTriangle(B[k], B[(k + 1) % 3], A[0], A[1], A[2])) return true;
  }
  return false;
}

export const countSelfIntersections = (m: Mesh): number => intersectingPairs(m).length;

export function intersectingPairs(m: Mesh): [number, number][] {
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
  const hits: [number, number][] = [];
  for (let x = 0; x < nt; x++) {
    const i = order[x], bi = box[i];
    for (let y = x + 1; y < nt; y++) {
      const j = order[y], bj = box[j];
      if (bj[0] > bi[1]) break;
      if (bj[2] > bi[3] || bj[3] < bi[2] || bj[4] > bi[5] || bj[5] < bi[4]) continue;
      if (idx[i].some((k) => idx[j].includes(k))) continue;
      if (trianglesIntersect(tri[i], tri[j])) hits.push([i, j]);
    }
  }
  return hits;
}

export function intersections(model: Model, n = 40): { self: number; contacts: number } {
  const meshes = parts(model).map((sh) => buildMesh(sh, n, n, false));
  const owner: number[] = [];
  meshes.forEach((m, k) => owner.push(...new Array<number>(m.indices.length / 3).fill(k)));
  let self = 0, contacts = 0;
  for (const [a, b] of intersectingPairs(merge(meshes))) owner[a] === owner[b] ? self++ : contacts++;
  return { self, contacts };
}

export const selfIntersections = (model: Model, n = 40): number => intersections(model, n).self;
export const contacts = (model: Model, n = 40): number => intersections(model, n).contacts;
