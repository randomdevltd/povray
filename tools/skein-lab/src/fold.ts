import { Sample } from './surface.ts';
import type { Op, Surface } from './surface.ts';
import { add, cross, dot, normalize, reject, scale, X, Y } from './vec.ts';
import type { Vec3 } from './vec.ts';
import { place, rmf } from './frames.ts';
import type { Frame } from './frames.ts';
import { field } from './value.ts';
import type { Vec2 } from './series.ts';

export class FoldSample extends Sample {
  s = 0;
  t = 0;
  w = 0;
  theta = 0;
}

export type Cyl = [number, number, number] | { radius?: number; angle?: number; axial?: number };
export type FoldValue<T> = T | ((c: FoldSample) => T);
export type Curve = (t: number, c: FoldSample) => readonly number[];

export interface FoldOptions {
  axis?: Vec3 | ((t: number) => Vec3);
  origin?: Vec3;
  along?: Vec3;
  radius?: FoldValue<number | Cyl>;
  curve?: Curve;
  twist?: FoldValue<number>;
  axial?: FoldValue<number>;
  range?: number;
  start?: number;
  up?: Vec3;
  roll?: number;
  correct?: boolean;
}

const TAU = 2 * Math.PI;

function cyl(out: number | Cyl): [number, number, number] {
  if (typeof out === 'number') return [out, 0, 0];
  if (Array.isArray(out)) return out;
  return [out.radius ?? 1, out.angle ?? 0, out.axial ?? 0];
}

export function fold(o: FoldOptions = {}): Op {
  const path = typeof o.axis === 'function' ? o.axis : undefined;
  const axis = path || !o.axis ? Y : normalize(o.axis as Vec3);
  const along = normalize(reject(o.along ?? (Math.abs(axis[0]) > 0.9 ? Y : X), axis));
  const offset = cross(along, axis);
  const range = o.range ?? TAU, start = o.start ?? 0;
  const origin = o.origin ?? [0, 0, 0];
  const straight = (t: number): Frame => ({ pos: add(origin, scale(axis, t)), T: axis, e1: along, e2: cross(axis, along) });
  const frame = path ? rmf(path, { up: o.up, twist: o.roll, correct: o.correct }).frame : straight;
  const radius = field((o.radius ?? 1) as never) as (c: FoldSample) => number | Cyl;
  const twist = o.twist === undefined ? () => 0 : (field(o.twist as never) as (c: FoldSample) => number);
  const axial = o.axial === undefined ? () => 0 : (field(o.axial as never) as (c: FoldSample) => number);
  const curve = o.curve;

  const section = (c: FoldSample): [number, number, number] => {
    if (curve) {
      const t = c.theta / TAU, h = 1e-5, turn = twist(c);
      const [a, b] = curve(t, c), [a1, b1] = curve(t + h, c), [a0, b0] = curve(t - h, c);
      const ta = a1 - a0, tb = b1 - b0, l = Math.hypot(ta, tb) || 1;
      const pa = a + (c.w * tb) / l, pb = b - (c.w * ta) / l;
      const cs = Math.cos(turn), sn = Math.sin(turn);
      return [pa * cs - pb * sn, pa * sn + pb * cs, axial(c)];
    }
    const [rad, dAngle, dAxial] = cyl(radius(c));
    const rho = rad + c.w, ang = c.theta + dAngle + twist(c);
    return [rho * Math.cos(ang), rho * Math.sin(ang), dAxial + axial(c)];
  };

  return (prev: Surface) => (u, v) => {
    const c = new FoldSample(prev, u, v);
    c.s = dot(c.p, along);
    c.t = dot(c.p, axis);
    c.w = dot(c.p, offset);
    c.theta = start + c.s * range;
    const [a, b, dAxial] = section(c);
    return place(frame(c.t + dAxial), a, b);
  };
}

export const lathe = (profile: FoldValue<Vec2>, o: Omit<FoldOptions, 'radius' | 'curve' | 'axial'> = {}): Op => {
  const f = field(profile as never) as (c: FoldSample) => Vec2;
  return fold({ ...o, radius: (c) => { const [r, h] = f(c); return { radius: r, axial: h - c.t }; } });
};
