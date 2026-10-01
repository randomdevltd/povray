import { series } from './value.ts';
import type { Series } from './value.ts';
import type { Vec3 } from './vec.ts';

const { sin, cos, PI } = Math;

export const helix = (radius: number, height: number, turns: number): Series<Vec3> =>
  series((t) => [radius * cos(2 * PI * turns * t), height * t, -radius * sin(2 * PI * turns * t)]);

export const torusKnot = (p: number, q: number, R = 2, r = 1): Series<Vec3> =>
  series((t) => {
    const a = 2 * PI * t, d = R + r * cos(q * a);
    return [d * cos(p * a), d * sin(p * a), -r * sin(q * a)];
  });
