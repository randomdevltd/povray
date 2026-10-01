import { shape, fold, displace, series, spline, fbm, noise } from '../src/index.ts';
import type { Vec3 } from '../src/index.ts';

const { cos, sin, exp, sqrt, min, PI } = Math;

const coil = series((t): Vec3 => {
  const a = 1.6 * 2 * PI * t, r = 1.2 * exp(-1.1 * t);
  return [r * cos(a), r * sin(a), 0.7 * t];
});
const taper = spline([0, 0.4], [0.5, 0.22], [0.85, 0.1], [1, 0.05]);
const growth = (v: number) => 1 + 0.05 * (1 - ((28 * v) % 1));
const keel = (theta: number) => 1 + 0.12 * cos(3 * theta);

const octaves = ([x, y, z]: Vec3) =>
  0.03 * fbm([3 * x, 3 * y, 3 * z], 2) + 0.01 * fbm([12 * x, 12 * y, 12 * z], 2) + 0.003 * noise([40 * x, 40 * y, 40 * z]);

export default shape({ wrap: 'u', ends: ['flat', 'pole'] },
  fold({
    axis: coil,
    radius: ({ v, theta }) => taper(v) * keel(theta) * growth(v) * sqrt(min(1, 12 * (1 - v))),
    twist: ({ v }) => 2.5 * v,
  }),
  displace(({ v, p }) => min(1, 40 * v) * octaves(p)),
);
