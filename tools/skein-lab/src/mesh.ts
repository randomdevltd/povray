import { normalAt, ops, unitQuad } from './surface.ts';
import type { Op, Surface } from './surface.ts';
import { add, cross, dist, dot, norm, scale, sub } from './vec.ts';
import type { Vec3 } from './vec.ts';

export type End = 'open' | 'flat' | 'pole';
export interface Topology {
  wrap?: 'none' | 'u' | 'v' | 'uv';
  ends?: End | [End, End];
  grid?: [number, number];
  shift?: number;
  flip?: boolean;
}
export type Model = Shape | Shape[];
export const parts = (m: Model): Shape[] => (Array.isArray(m) ? m : [m]);

export interface Shape {
  surface: Surface;
  wrapU: boolean;
  wrapV: boolean;
  ends: [End, End];
  grid: [number, number];
  shift: number;
  flip: boolean;
}

export function shape(topo: Topology, ...list: Op[]): Shape {
  const wrap = topo.wrap ?? 'none';
  const e = topo.ends ?? 'open';
  return {
    surface: ops(...list)(unitQuad),
    wrapU: wrap === 'u' || wrap === 'uv',
    wrapV: wrap === 'v' || wrap === 'uv',
    ends: typeof e === 'string' ? [e, e] : e,
    grid: topo.grid ?? [160, 160],
    shift: topo.shift ?? 0,
    flip: topo.flip ?? false,
  };
}

export interface Mesh {
  positions: Float64Array;
  normals: Float32Array;
  indices: Uint32Array;
  surfaceTris: number;
  closed: boolean;
  grid: Vec3[][];
}

function earClip(ring: Vec3[]): [number, number, number][] {
  let n: Vec3 = [0, 0, 0];
  ring.forEach((p, i) => (n = add(n, cross(p, ring[(i + 1) % ring.length]))));
  const e1 = sub(ring[1], ring[0]), e2 = cross(n, e1);
  const pts = ring.map((p) => [dot(p, e1), dot(p, e2)]);
  const area2 = (a: number, b: number, c: number) =>
    (pts[b][0] - pts[a][0]) * (pts[c][1] - pts[a][1]) - (pts[c][0] - pts[a][0]) * (pts[b][1] - pts[a][1]);
  const span = Math.max(...pts.map(([x, y]) => Math.abs(x) + Math.abs(y)));
  const tol = 1e-9 * span * span;
  const idx = ring.map((_, i) => i), out: [number, number, number][] = [];
  while (idx.length > 3) {
    let clipped = false;
    for (let k = 0; k < idx.length && !clipped; k++) {
      const a = idx[(k + idx.length - 1) % idx.length], b = idx[k], c = idx[(k + 1) % idx.length];
      if (area2(a, b, c) <= tol) continue;
      const inside = idx.some((q) => q !== a && q !== b && q !== c && area2(a, b, q) >= -tol && area2(b, c, q) >= -tol && area2(c, a, q) >= -tol);
      if (inside) continue;
      out.push([a, b, c]);
      idx.splice(k, 1);
      clipped = true;
    }
    if (!clipped) break;
  }
  if (idx.length === 3) out.push([idx[0], idx[1], idx[2]]);
  return out;
}

function seamU(sh: Shape, i: number, nu: number): number {
  const k = Math.round(sh.shift * nu), m = sh.flip ? k + nu - i : i + k;
  return sh.wrapU ? ((m % nu) + nu) % nu : m;
}

export function buildMesh(sh: Shape, nu = sh.grid[0], nv = sh.grid[1], withNormals = true): Mesh {
  const grid: Vec3[][] = [];
  for (let i = 0; i <= nu; i++) {
    grid.push([]);
    for (let j = 0; j <= nv; j++) grid[i].push(sh.surface(i / nu, j / nv));
  }
  const ends: End[] = sh.wrapV ? ['open', 'open'] : sh.ends;
  const ids = new Int32Array((nu + 1) * (nv + 1)).fill(-1);
  const pos: Vec3[] = [], count: number[] = [], uv: [number, number][] = [];
  const pole = [-1, -1];
  const vid = (i: number, j: number): number => {
    const seam = sh.wrapV && j === nv;
    const ii = seam ? seamU(sh, i, nu) : sh.wrapU && i === nu ? 0 : i, jj = seam ? 0 : j;
    const end = jj === 0 ? 0 : jj === nv ? 1 : -1;
    const slot = jj * (nu + 1) + ii;
    if (end >= 0 && ends[end] === 'pole' && pole[end] >= 0) ids[slot] = pole[end];
    if (ids[slot] < 0) {
      ids[slot] = pos.length;
      pos.push([0, 0, 0]);
      count.push(0);
      uv.push([ii / nu, jj / nv]);
      if (end >= 0 && ends[end] === 'pole') pole[end] = ids[slot];
    }
    return ids[slot];
  };
  for (let j = 0; j <= nv; j++)
    for (let i = 0; i <= nu; i++) {
      const k = vid(i, j);
      if (count[k] === 0 || !(sh.wrapU && i === nu) && !(sh.wrapV && j === nv)) {
        pos[k] = add(pos[k], grid[i][j]);
        count[k]++;
      }
    }
  for (let k = 0; k < pos.length; k++) pos[k] = scale(pos[k], 1 / count[k]);

  const tris: number[] = [];
  const tri = (a: number, b: number, c: number) => {
    if (a !== b && b !== c && a !== c) tris.push(a, b, c);
  };
  for (let i = 0; i < nu; i++)
    for (let j = 0; j < nv; j++) {
      const a = vid(i, j), b = vid(i + 1, j), c = vid(i, j + 1), d = vid(i + 1, j + 1);
      tri(a, b, c);
      tri(b, d, c);
    }
  const surfaceTris = tris.length / 3, gridVerts = pos.length;
  for (const end of [0, 1] as const) {
    if (!sh.wrapU || ends[end] !== 'flat') continue;
    const ring = Array.from({ length: nu }, (_, i) => vid(i, end === 0 ? 0 : nv));
    for (const [a, b, c] of earClip(ring.map((k) => pos[k]))) end === 0 ? tri(ring[c], ring[b], ring[a]) : tri(ring[a], ring[b], ring[c]);
  }
  const closed = !sh.flip && sh.wrapU && (sh.wrapV || (ends[0] !== 'open' && ends[1] !== 'open'));

  const normals = new Float32Array(pos.length * 3);
  if (withNormals) for (let k = 0; k < gridVerts; k++) normals.set(normalAt(sh.surface, uv[k][0], uv[k][1]), k * 3);
  return { positions: new Float64Array(pos.flat()), normals, indices: new Uint32Array(tris), surfaceTris, closed, grid };
}

const vert = (m: Mesh, k: number): Vec3 => [m.positions[3 * k], m.positions[3 * k + 1], m.positions[3 * k + 2]];

export function area(m: Mesh): number {
  let a = 0;
  for (let t = 0; t < m.surfaceTris; t++) {
    const [p, q, r] = [0, 1, 2].map((k) => vert(m, m.indices[3 * t + k]));
    a += norm(cross(sub(q, p), sub(r, p))) / 2;
  }
  return a;
}

export function volume(m: Mesh): number | null {
  if (!m.closed) return null;
  let v = 0;
  for (let t = 0; t < m.indices.length / 3; t++) {
    const [p, q, r] = [0, 1, 2].map((k) => vert(m, m.indices[3 * t + k]));
    v += dot(p, cross(q, r)) / 6;
  }
  return v;
}

export function seamGap(sh: Shape, m: Mesh): number {
  const g = m.grid, nu = g.length - 1, nv = g[0].length - 1;
  let gap = 0;
  if (sh.wrapU) for (let j = 0; j <= nv; j++) gap = Math.max(gap, dist(g[0][j], g[nu][j]));
  if (sh.wrapV) for (let i = 0; i <= nu; i++) gap = Math.max(gap, dist(g[seamU(sh, i, nu)][0], g[i][nv]));
  return gap;
}

export function merge(meshes: Mesh[]): Mesh {
  let offset = 0;
  const idx: number[] = [], pos: number[] = [];
  for (const m of meshes) {
    for (const k of m.indices) idx.push(k + offset);
    pos.push(...m.positions);
    offset += m.positions.length / 3;
  }
  return { positions: new Float64Array(pos), normals: new Float32Array(pos.length), indices: new Uint32Array(idx), surfaceTris: 0, closed: false, grid: [] };
}
