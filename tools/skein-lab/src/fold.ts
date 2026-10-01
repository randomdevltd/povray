import { Sample } from './surface.ts';
import type { Op, Surface } from './surface.ts';
import { add, cross, dot, normalize, reject, scale, X, Y } from './vec.ts';
import type { Vec3 } from './vec.ts';
import { place, rmf } from './frames.ts';
import type { Frame, Path } from './frames.ts';

export class FoldSample extends Sample {
  s = 0;
  t = 0;
  w = 0;
  theta = 0;
}

export type Radius = number | ((c: FoldSample) => number | Vec3);
export type Curve = (t: number, c: FoldSample) => [number, number];

export interface FoldOptions {
  axis?: Vec3 | Path;
  origin?: Vec3;
  along?: Vec3;
  radius?: Radius;
  curve?: Curve;
  range?: number;
  start?: number;
  up?: Vec3;
  twist?: number;
  correct?: boolean;
}

function section(o: FoldOptions, c: FoldSample): [number, number, number] {
  if (o.curve) {
    const curve = o.curve, t = c.theta / (2 * Math.PI), h = 1e-5;
    const [a, b] = curve(t, c), [a1, b1] = curve(t + h, c), [a0, b0] = curve(t - h, c);
    const ta = a1 - a0, tb = b1 - b0, l = Math.hypot(ta, tb) || 1;
    return [a + (c.w * tb) / l, b - (c.w * ta) / l, 0];
  }
  const r = o.radius ?? 1;
  const out = typeof r === 'number' ? r : r(c);
  const [rad, dAngle, dAxial] = typeof out === 'number' ? [out, 0, 0] : out;
  const rho = rad + c.w, ang = c.theta + dAngle;
  return [rho * Math.cos(ang), rho * Math.sin(ang), dAxial];
}

export function fold(o: FoldOptions = {}): Op {
  const path = typeof o.axis === 'function' ? o.axis : undefined;
  const axis = path || !o.axis ? Y : normalize(o.axis as Vec3);
  const along = normalize(reject(o.along ?? (Math.abs(axis[0]) > 0.9 ? Y : X), axis));
  const offset = cross(along, axis);
  const range = o.range ?? 2 * Math.PI, start = o.start ?? 0;
  const origin = o.origin ?? [0, 0, 0];
  const straight = (t: number): Frame => ({ pos: add(origin, scale(axis, t)), T: axis, e1: along, e2: cross(axis, along) });
  const frame = path ? rmf(path, { up: o.up, twist: o.twist, correct: o.correct }).frame : straight;

  return (prev: Surface) => (u, v) => {
    const c = new FoldSample(prev, u, v);
    c.s = dot(c.p, along);
    c.t = dot(c.p, axis);
    c.w = dot(c.p, offset);
    c.theta = start + c.s * range;
    const [a, b, dAxial] = section(o, c);
    return place(frame(c.t + dAxial), a, b);
  };
}
