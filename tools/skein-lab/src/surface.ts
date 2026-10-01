import { cross, norm, sub, scale, add } from './vec.ts';
import type { Vec3 } from './vec.ts';

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

export type Field<T> = (s: Sample) => T;

export const ops = (...list: Op[]): Op => (prev) => list.reduce((s, op) => op(s), prev);

export function fn(...f: [Field<Vec3>] | [Field<number>, Field<number>, Field<number>]): Op {
  const g: Field<Vec3> = f.length === 3 ? (s) => [f[0](s), f[1](s), f[2](s)] : f[0];
  return (prev) => (u, v) => g(new Sample(prev, u, v));
}

export const displace = (f: Field<number>): Op => (prev) => (u, v) => {
  const s = new Sample(prev, u, v);
  return add(s.p, scale(s.n, f(s)));
};

export function spherical(radius: number | Field<number> = 1): Op {
  const r = typeof radius === 'number' ? () => radius : radius;
  return fn((s) => {
    const theta = 2 * Math.PI * s.u, phi = Math.PI * s.v, k = r(s);
    return [k * Math.sin(phi) * Math.cos(theta), -k * Math.cos(phi), -k * Math.sin(phi) * Math.sin(theta)];
  });
}
