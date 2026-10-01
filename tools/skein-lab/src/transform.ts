import { Sample } from './surface.ts';
import type { Op } from './surface.ts';
import { add, cross, dot, normalize, reject, rotate as turn, scale as times, sub } from './vec.ts';
import type { Vec3 } from './vec.ts';
import { field, isConstant } from './value.ts';
import type { Value } from './value.ts';

export type Matrix = number[][];

function pointwise<T>(value: Value<T>, apply: (p: Vec3, k: T) => Vec3): Op {
  if (isConstant(value)) return (prev) => (u, v) => apply(prev(u, v), value);
  const f = field(value);
  return (prev) => (u, v) => {
    const s = new Sample(prev, u, v);
    return apply(s.p, f(s));
  };
}

export const scale = (k: Value<number | Vec3>, origin: Vec3 = [0, 0, 0]): Op =>
  pointwise(k, (p, k) => {
    const [a, b, c] = typeof k === 'number' ? [k, k, k] : k;
    return [origin[0] + a * (p[0] - origin[0]), origin[1] + b * (p[1] - origin[1]), origin[2] + c * (p[2] - origin[2])];
  });

export const translate = (d: Value<Vec3>): Op => pointwise(d, add);

export function rotate(axis: Vec3, angle: Value<number>, origin: Vec3 = [0, 0, 0]): Op {
  const a = normalize(axis);
  return pointwise(angle, (p, k) => add(origin, turn(sub(p, origin), a, k)));
}

export function applyMatrix(m: Matrix, p: Vec3): Vec3 {
  const row = (r: number[]) => r[0] * p[0] + r[1] * p[1] + r[2] * p[2] + (r[3] ?? 0);
  const q: Vec3 = [row(m[0]), row(m[1]), row(m[2])];
  const w = m.length === 4 ? row(m[3]) : 1;
  return w === 1 ? q : [q[0] / w, q[1] / w, q[2] / w];
}

export const matrix = (m: Value<Matrix>): Op => pointwise(m, (p, k) => applyMatrix(k, p));

export interface BendOptions {
  origin?: Vec3;
  side?: Vec3;
  toward?: Vec3;
  radius: Value<number>;
  angle?: Value<number>;
}

export function bend(o: BendOptions): Op {
  const origin = o.origin ?? [0, 0, 0], n = normalize(o.toward ?? [0, 0, 1]);
  const e = normalize(reject(o.side ?? [1, 0, 0], n)), a = cross(n, e);
  const radius = field(o.radius), limit = field(o.angle ?? Infinity);
  return (prev) => (u, v) => {
    const s = new Sample(prev, u, v), d = sub(s.p, origin);
    const rho = dot(d, e);
    if (rho <= 0) return s.p;
    const R = radius(s), h = dot(d, n), phi = Math.min(rho / R, limit(s)), extra = rho - phi * R;
    const c = Math.cos(phi), sn = Math.sin(phi);
    const radial = add(times(e, (R - h) * sn), times(n, R - (R - h) * c));
    return add(add(origin, times(a, dot(d, a))), add(radial, add(times(e, extra * c), times(n, extra * sn))));
  };
}
