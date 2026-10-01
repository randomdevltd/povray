import { add, cross, dist, dot, lerp, norm, normalize, reject, rotate, scale, sub, X, Z } from './vec.ts';
import type { Vec3 } from './vec.ts';

export type Path = (t: number) => Vec3;
export interface Frame { pos: Vec3; T: Vec3; e1: Vec3; e2: Vec3 }
export interface FrameOptions { up?: Vec3; twist?: number; samples?: number; correct?: boolean }

export const tangent = (path: Path, t: number): Vec3 => normalize(sub(path(t + 1e-5), path(t - 1e-5)));

export function pathLength(path: Path, samples = 20000): number {
  let len = 0, prev = path(0);
  for (let i = 1; i <= samples; i++) {
    const p = path(i / samples);
    len += dist(p, prev);
    prev = p;
  }
  return len;
}

// why: double reflection is Wang et al. 2008, "Computation of rotation minimizing frames"
export function rmf(path: Path, { up = Z, twist = 0, samples = 2048, correct = true }: FrameOptions = {}) {
  const xs: Vec3[] = [], ts: Vec3[] = [], rs: Vec3[] = [];
  for (let i = 0; i <= samples; i++) {
    xs.push(path(i / samples));
    ts.push(tangent(path, i / samples));
  }
  let r0 = reject(up, ts[0]);
  if (norm(r0) < 1e-6) r0 = reject(X, ts[0]);
  rs.push(normalize(r0));
  for (let i = 0; i < samples; i++) {
    const v1 = sub(xs[i + 1], xs[i]), c1 = dot(v1, v1);
    const rL = c1 > 0 ? sub(rs[i], scale(v1, (2 / c1) * dot(v1, rs[i]))) : rs[i];
    const tL = c1 > 0 ? sub(ts[i], scale(v1, (2 / c1) * dot(v1, ts[i]))) : ts[i];
    const v2 = sub(ts[i + 1], tL), c2 = dot(v2, v2);
    rs.push(c2 > 1e-30 ? sub(rL, scale(v2, (2 / c2) * dot(v2, rL))) : rL);
  }
  const extent = Math.max(...xs.map(norm), 1);
  const closed = dist(xs[0], xs[samples]) < 1e-9 * extent;
  const holonomy = closed && correct ? Math.atan2(dot(cross(rs[0], rs[samples]), ts[0]), dot(rs[0], rs[samples])) : 0;

  const frame = (t: number): Frame => {
    const tt = closed && (t < 0 || t > 1) ? t - Math.floor(t) : Math.min(1, Math.max(0, t));
    const f = tt * samples, i = Math.min(samples - 1, Math.floor(f));
    const T = tangent(path, t);
    const r = normalize(reject(lerp(rs[i], rs[i + 1], f - i), T));
    const e1 = rotate(r, T, (twist - holonomy) * tt);
    return { pos: path(t), T, e1, e2: cross(T, e1) };
  };
  return { frame, closed, holonomy };
}

export const place = (f: Frame, a: number, b: number): Vec3 => add(f.pos, add(scale(f.e1, a), scale(f.e2, b)));
