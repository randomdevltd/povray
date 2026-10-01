import { series } from './value.ts';
import type { Series } from './value.ts';
import type { Vec3 } from './vec.ts';

export type Vec2 = [number, number];
export type Interp = 'linear' | 'catmull' | 'cubic';

export interface PathOptions {
  closed?: boolean;
  interp?: Interp;
  at?: number[];
  arclength?: boolean;
}

export type Knot<T> = [number, T];

type Row = number[];

function solve(a: number[][], b: number[]): number[] {
  const n = b.length;
  for (let i = 0; i < n; i++) {
    let p = i;
    for (let r = i + 1; r < n; r++) if (Math.abs(a[r][i]) > Math.abs(a[p][i])) p = r;
    [a[i], a[p], b[i], b[p]] = [a[p], a[i], b[p], b[i]];
    for (let r = i + 1; r < n; r++) {
      const f = a[r][i] / a[i][i];
      for (let c = i; c < n; c++) a[r][c] -= f * a[i][c];
      b[r] -= f * b[i];
    }
  }
  const x = new Array<number>(n).fill(0);
  for (let i = n - 1; i >= 0; i--) {
    let s = b[i];
    for (let c = i + 1; c < n; c++) s -= a[i][c] * x[c];
    x[i] = s / a[i][i];
  }
  return x;
}

function secondDerivatives(ys: number[], ts: number[], closed: boolean): number[] {
  const n = ys.length, segs = closed ? n : n - 1;
  const h = (i: number) => ts[i + 1] - ts[i];
  const y = (i: number) => ys[i % n];
  const a = Array.from({ length: n }, () => new Array<number>(n).fill(0)), b = new Array<number>(n).fill(0);
  for (let i = 0; i < n; i++) {
    if (!closed && (i === 0 || i === n - 1)) {
      a[i][i] = 1;
      continue;
    }
    const prev = (i - 1 + segs) % segs, hp = h(prev), hn = h(i % segs);
    a[i][(i - 1 + n) % n] += hp / 6;
    a[i][i] += (hp + hn) / 3;
    a[i][(i + 1) % n] += hn / 6;
    b[i] = (y(i + 1) - y(i)) / hn - (y(i) - y(i - 1 + n)) / hp;
  }
  return solve(a, b);
}

function interpolator(rows: Row[], o: PathOptions): (t: number) => Row {
  const closed = o.closed ?? false, interp = o.interp ?? 'linear';
  const n = rows.length, segs = closed ? n : n - 1, dim = rows[0].length;
  const ts = o.at ? [...o.at] : Array.from({ length: n }, (_, i) => i / segs);
  if (closed && ts.length === n) ts.push(1);
  const at = (i: number): Row => rows[((i % n) + n) % n];
  const ghost = (i: number): Row => {
    if (closed || (i >= 0 && i < n)) return at(i);
    const [p, q] = i < 0 ? [rows[0], rows[1]] : [rows[n - 1], rows[n - 2]];
    return p.map((v, k) => 2 * v - q[k]);
  };
  const m2 = interp === 'cubic' ? Array.from({ length: dim }, (_, k) => secondDerivatives(rows.map((r) => r[k]), ts, closed)) : [];

  return (t) => {
    const lo = ts[0], hi = ts[segs];
    const tt = closed ? lo + ((((t - lo) % (hi - lo)) + (hi - lo)) % (hi - lo)) : Math.min(hi, Math.max(lo, t));
    let i = 0;
    while (i < segs - 1 && tt >= ts[i + 1]) i++;
    const h = ts[i + 1] - ts[i], s = h > 0 ? (tt - ts[i]) / h : 0;
    const p1 = at(i), p2 = at(i + 1);
    if (interp === 'linear') return p1.map((v, k) => v + (p2[k] - v) * s);
    if (interp === 'catmull') {
      const p0 = ghost(i - 1), p3 = ghost(i + 2);
      return p1.map((b, k) => {
        const a = p0[k], c = p2[k], d = p3[k];
        return 0.5 * (2 * b + (c - a) * s + (2 * a - 5 * b + 4 * c - d) * s * s + (3 * b - a - 3 * c + d) * s * s * s);
      });
    }
    return p1.map((v, k) => {
      const ma = m2[k][i % n], mb = m2[k][(i + 1) % n], w = p2[k];
      return (1 - s) * v + s * w + ((h * h) / 6) * (((1 - s) ** 3 - (1 - s)) * ma + (s ** 3 - s) * mb);
    });
  };
}

function reparameterise(f: (t: number) => Row, samples = 2048): (s: number) => Row {
  const len = [0];
  let prev = f(0);
  for (let i = 1; i <= samples; i++) {
    const p = f(i / samples);
    len.push(len[i - 1] + Math.hypot(...p.map((v, k) => v - prev[k])));
    prev = p;
  }
  const total = len[samples];
  return (s) => {
    const target = Math.min(1, Math.max(0, s)) * total;
    let lo = 0, hi = samples;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (len[mid] <= target) lo = mid;
      else hi = mid;
    }
    const span = len[hi] - len[lo];
    return f((lo + (span > 0 ? (target - len[lo]) / span : 0)) / samples);
  };
}

function build(values: (number | number[])[], o: PathOptions): Series<unknown> {
  const scalar = typeof values[0] === 'number';
  const rows = values.map((p) => (typeof p === 'number' ? [p] : [...p]));
  let f = interpolator(rows, o);
  if (o.arclength) {
    const g = reparameterise(f);
    f = o.closed ? (t) => g(t - Math.floor(t)) : g;
  }
  return series((t) => (scalar ? f(t)[0] : f(t)));
}

const isOptions = (x: unknown): x is PathOptions => typeof x === 'object' && x !== null && !Array.isArray(x);

function split<P>(args: unknown[]): [P[], PathOptions] {
  const last = args[args.length - 1];
  return isOptions(last) ? [args.slice(0, -1) as P[], last] : [args as P[], {}];
}

export function path(...points: Vec3[]): Series<Vec3>;
export function path(...args: [...Vec3[], PathOptions]): Series<Vec3>;
export function path(...points: Vec2[]): Series<Vec2>;
export function path(...args: [...Vec2[], PathOptions]): Series<Vec2>;
export function path(...points: number[]): Series<number>;
export function path(...args: [...number[], PathOptions]): Series<number>;
export function path(...args: unknown[]): Series<unknown> {
  const [points, o] = split<number | number[]>(args);
  return build(points, o);
}

export function spline(...knots: Knot<Vec3>[]): Series<Vec3>;
export function spline(...args: [...Knot<Vec3>[], PathOptions]): Series<Vec3>;
export function spline(...knots: Knot<Vec2>[]): Series<Vec2>;
export function spline(...args: [...Knot<Vec2>[], PathOptions]): Series<Vec2>;
export function spline(...knots: Knot<number>[]): Series<number>;
export function spline(...args: [...Knot<number>[], PathOptions]): Series<number>;
export function spline(...args: unknown[]): Series<unknown> {
  const [knots, o] = split<Knot<number | number[]>>(args);
  return build(knots.map((k) => k[1]), { interp: 'catmull', ...o, at: knots.map((k) => k[0]) });
}
