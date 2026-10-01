import { path } from './series.ts';
import type { Series } from './value.ts';
import type { Vec2 } from './series.ts';

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

export function star(points: number, outer = 1, inner = 0.5): Series<Vec2> {
  const corners = Array.from({ length: 2 * points }, (_, i): Vec2 => {
    const a = (Math.PI * i) / points, r = i % 2 ? inner : outer;
    return [r * Math.cos(a), r * Math.sin(a)];
  });
  return path(...corners, { closed: true });
}
