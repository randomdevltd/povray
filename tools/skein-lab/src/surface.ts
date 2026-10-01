import { add, cross, lerp, norm, scale, sub } from './vec.ts';
import type { Vec3 } from './vec.ts';
import { field } from './value.ts';
import type { Field, Value } from './value.ts';

export type Surface = (u: number, v: number) => Vec3;
export type Op = (prev: Surface) => Surface;

export const unitQuad: Surface = (u, v) => [u, v, 0];

const H = 1e-5;

export function normalAt(s: Surface, u: number, v: number): Vec3 {
  const inward = v < 0.5 ? 1 : -1;
  for (const nudge of [0, 1e-4, 1e-3, 1e-2]) {
    const w = v + inward * nudge;
    const du = sub(s(u + H, w), s(u - H, w));
    const dv = sub(s(u, w + H), s(u, w - H));
    const c = cross(du, dv);
    const l = norm(c), m = Math.max(norm(du), norm(dv));
    if (l > 1e-6 * m * m) return scale(c, 1 / l);
  }
  return [0, 0, 1];
}

export class Sample {
  u: number;
  v: number;
  p: Vec3;
  readonly #prev: Surface;
  #n: Vec3 | undefined;

  constructor(prev: Surface, u: number, v: number) {
    this.#prev = prev;
    this.u = u;
    this.v = v;
    this.p = prev(u, v);
  }

  get x(): number { return this.p[0]; }
  get y(): number { return this.p[1]; }
  get z(): number { return this.p[2]; }
  get n(): Vec3 { return (this.#n ??= normalAt(this.#prev, this.u, this.v)); }
}

export const ops = (...list: Op[]): Op => (prev) => list.reduce((s, op) => op(s), prev);

export function fn(...f: [Field<Vec3>] | [Field<number>, Field<number>, Field<number>]): Op {
  const g: Field<Vec3> = f.length === 3 ? (s) => [f[0](s), f[1](s), f[2](s)] : f[0];
  return (prev) => (u, v) => g(new Sample(prev, u, v));
}

export function displace(amount: Value<number>): Op {
  const f = field(amount);
  return (prev) => (u, v) => {
    const s = new Sample(prev, u, v);
    return add(s.p, scale(s.n, f(s)));
  };
}

export function spherical(radius: Value<number> = 1): Op {
  const r = field(radius);
  return fn((s) => {
    const theta = 2 * Math.PI * s.u, phi = Math.PI * s.v, k = r(s);
    return [k * Math.sin(phi) * Math.cos(theta), -k * Math.cos(phi), -k * Math.sin(phi) * Math.sin(theta)];
  });
}

export function morph(keys: Op[], t: Value<number>): Op {
  const w = field(t);
  return (prev) => {
    const ss = keys.map((k) => k(prev));
    return (u, v) => {
      const f = Math.min(1, Math.max(0, w(new Sample(prev, u, v)))) * (ss.length - 1);
      const i = Math.min(ss.length - 2, Math.floor(f));
      return lerp(ss[i](u, v), ss[i + 1](u, v), f - i);
    };
  };
}
