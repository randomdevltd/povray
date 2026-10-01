import type { Curve } from './fold.ts';

export type Polar = (c: { theta: number }) => number;

export const circle = (r = 1): Polar => () => r;

export function ngon(n: number, r = 1): Polar {
  const k = (2 * Math.PI) / n;
  return ({ theta }) => {
    const m = theta - k * Math.floor(theta / k);
    return (r * Math.cos(Math.PI / n)) / Math.cos(m - Math.PI / n);
  };
}

export const superellipse = (p: number, a = 1, b = a): Polar => ({ theta }) =>
  (Math.abs(Math.cos(theta) / a) ** p + Math.abs(Math.sin(theta) / b) ** p) ** (-1 / p);

export function closedSpline(points: [number, number][]): Curve {
  const n = points.length;
  return (t) => {
    const f = (t - Math.floor(t)) * n, i = Math.floor(f), s = f - i;
    const [p0, p1, p2, p3] = [-1, 0, 1, 2].map((k) => points[(i + k + n) % n]);
    const cr = (a: number, b: number, c: number, d: number) =>
      0.5 * (2 * b + (c - a) * s + (2 * a - 5 * b + 4 * c - d) * s * s + (3 * b - a - 3 * c + d) * s * s * s);
    return [cr(p0[0], p1[0], p2[0], p3[0]), cr(p0[1], p1[1], p2[1], p3[1])];
  };
}
