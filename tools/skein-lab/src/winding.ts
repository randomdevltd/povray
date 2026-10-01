import { buildMesh, merge, parts } from './mesh.ts';
import type { Mesh, Model } from './mesh.ts';

interface Crossing { x: number; s: number }

export class RayField {
  readonly lo: [number, number, number];
  readonly hi: [number, number, number];
  readonly #tris: Float64Array;
  readonly #bins: number[][];
  readonly #nb: number;

  constructor(m: Mesh, bins = 64) {
    const nt = m.indices.length / 3, P = m.positions;
    this.#tris = new Float64Array(nt * 9);
    this.lo = [Infinity, Infinity, Infinity];
    this.hi = [-Infinity, -Infinity, -Infinity];
    for (let t = 0; t < nt; t++)
      for (let k = 0; k < 3; k++)
        for (let d = 0; d < 3; d++) {
          const x = P[3 * m.indices[3 * t + k] + d];
          this.#tris[9 * t + 3 * k + d] = x;
          this.lo[d] = Math.min(this.lo[d], x);
          this.hi[d] = Math.max(this.hi[d], x);
        }
    this.#nb = bins;
    this.#bins = Array.from({ length: bins * bins }, () => []);
    for (let t = 0; t < nt; t++) {
      const T = this.#tris, ys = [T[9 * t + 1], T[9 * t + 4], T[9 * t + 7]], zs = [T[9 * t + 2], T[9 * t + 5], T[9 * t + 8]];
      const [y0, y1] = [this.#bin(Math.min(...ys), 1), this.#bin(Math.max(...ys), 1)];
      const [z0, z1] = [this.#bin(Math.min(...zs), 2), this.#bin(Math.max(...zs), 2)];
      for (let i = y0; i <= y1; i++) for (let j = z0; j <= z1; j++) this.#bins[i * bins + j].push(t);
    }
  }

  #bin(x: number, d: 1 | 2): number {
    const f = (x - this.lo[d]) / (this.hi[d] - this.lo[d] || 1);
    return Math.min(this.#nb - 1, Math.max(0, Math.floor(f * this.#nb)));
  }

  crossings(y: number, z: number): Crossing[] {
    const out: Crossing[] = [], T = this.#tris;
    for (const t of this.#bins[this.#bin(y, 1) * this.#nb + this.#bin(z, 2)]) {
      const o = 9 * t;
      const ay = T[o + 1] - y, az = T[o + 2] - z, by = T[o + 4] - y, bz = T[o + 5] - z, cy = T[o + 7] - y, cz = T[o + 8] - z;
      const w0 = by * cz - bz * cy, w1 = cy * az - cz * ay, w2 = ay * bz - az * by, sum = w0 + w1 + w2;
      if (sum === 0 || (w0 < 0 || w1 < 0 || w2 < 0) && (w0 > 0 || w1 > 0 || w2 > 0)) continue;
      out.push({ x: (w0 * T[o] + w1 * T[o + 3] + w2 * T[o + 6]) / sum, s: sum > 0 ? -1 : 1 });
    }
    return out.sort((a, b) => a.x - b.x);
  }
}

const JITTER = [0.5 + 1e-7 * Math.SQRT2, 0.5 + 1e-7 * Math.PI];

export function unionVolume(model: Model, rays = 256): { union: number; winding: number; maxWinding: number; minWinding: number } {
  const field = new RayField(merge(parts(model).map((sh) => buildMesh(sh, undefined, undefined, false))));
  const dy = (field.hi[1] - field.lo[1]) / rays, dz = (field.hi[2] - field.lo[2]) / rays;
  let union = 0, winding = 0, maxW = 0, minW = 0;
  for (let i = 0; i < rays; i++)
    for (let j = 0; j < rays; j++) {
      let w = 0, last = 0;
      for (const c of field.crossings(field.lo[1] + (i + JITTER[0]) * dy, field.lo[2] + (j + JITTER[1]) * dz)) {
        if (w !== 0) union += c.x - last;
        winding += w * (c.x - last);
        w += c.s;
        last = c.x;
        maxW = Math.max(maxW, w);
        minW = Math.min(minW, w);
      }
    }
  return { union: union * dy * dz, winding: winding * dy * dz, maxWinding: maxW, minWinding: minW };
}

export function windingSlice(model: Model, z: number, n = 256): { w: Int8Array; n: number; lo: number[]; hi: number[] } {
  const field = new RayField(merge(parts(model).map((sh) => buildMesh(sh, undefined, undefined, false))));
  const lo = [field.lo[0], field.lo[1]], hi = [field.hi[0], field.hi[1]], w = new Int8Array(n * n);
  for (let j = 0; j < n; j++) {
    const y = lo[1] + ((j + JITTER[0]) / n) * (hi[1] - lo[1]);
    const cs = field.crossings(y, z + 1e-7 * Math.E);
    let k = 0, acc = 0;
    for (let i = 0; i < n; i++) {
      const x = lo[0] + ((i + 0.5) / n) * (hi[0] - lo[0]);
      while (k < cs.length && cs[k].x < x) acc += cs[k++].s;
      w[j * n + i] = acc;
    }
  }
  return { w, n, lo, hi };
}

export interface Orientation { orientable: boolean; boundaryEdges: number; nonManifoldEdges: number }

export function orientation(m: Mesh): Orientation {
  const nt = m.indices.length / 3, edges = new Map<string, [number, number][]>();
  for (let t = 0; t < nt; t++)
    for (let k = 0; k < 3; k++) {
      const a = m.indices[3 * t + k], b = m.indices[3 * t + ((k + 1) % 3)];
      const key = a < b ? `${a},${b}` : `${b},${a}`;
      edges.set(key, [...(edges.get(key) ?? []), [t, a < b ? 1 : -1]]);
    }
  const adj: [number, number][][] = Array.from({ length: nt }, () => []);
  let boundaryEdges = 0, nonManifoldEdges = 0;
  for (const list of edges.values()) {
    if (list.length === 1) boundaryEdges++;
    if (list.length > 2) nonManifoldEdges++;
    if (list.length !== 2) continue;
    const [[ta, da], [tb, db]] = list, rel = -da * db;
    adj[ta].push([tb, rel]);
    adj[tb].push([ta, rel]);
  }
  const flip = new Int8Array(nt);
  let orientable = true;
  for (let s = 0; s < nt; s++) {
    if (flip[s]) continue;
    flip[s] = 1;
    const queue = [s];
    while (queue.length) {
      const t = queue.pop()!;
      for (const [o, rel] of adj[t]) {
        const want = flip[t] * rel;
        if (!flip[o]) {
          flip[o] = want;
          queue.push(o);
        } else if (flip[o] !== want) orientable = false;
      }
    }
  }
  return { orientable, boundaryEdges, nonManifoldEdges };
}

export const orientationOf = (model: Model): Orientation[] => parts(model).map((sh) => orientation(buildMesh(sh, 48, 48, false)));
