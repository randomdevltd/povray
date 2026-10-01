import { shape, fold, displace, path, spline, fbm, noise } from '../src/index.ts';
import type { Vec3 } from '../src/index.ts';

const { abs, sin, exp, sqrt, min, cos, PI } = Math;

const grain = path([0, 0, 0], [0.08, 1.2, 0.05], [-0.06, 2.4, -0.04], [0.05, 3.5, 0.08], [0, 4.4, 0], { interp: 'catmull', arclength: true });
const girth = spline([0, 0.09], [0.4, 0.075], [0.8, 0.08], [0.88, 0.07], [0.94, 0.15], [1, 0.12]);
const wrapped = (d: number) => ((d + 1.5) % 1) - 0.5;
const knot = (u: number, v: number, at: [number, number]) => exp(-(wrapped(u - at[0]) ** 2) / 0.004 - (v - at[1]) ** 2 / 0.0004);

const bark = (u: number, v: number, [x, y, z]: Vec3) =>
  -0.01 * abs(sin(PI * (14 * u + 9 * v))) + 0.012 * fbm([6 * x, 6 * y, 6 * z], 3) + 0.003 * noise([50 * x, 50 * y, 50 * z]);
const carving = (u: number, v: number) => (v > 0.9 ? -0.012 * abs(cos(PI * (6 * u + 60 * v))) : 0);

const staff = shape({ wrap: 'u', ends: 'pole', grid: [96, 640] },
  fold({ axis: grain, radius: ({ v }) => girth(v) * sqrt(min(1, 25 * (1 - v), 60 * v)) }),
  displace(({ u, v, p }) => bark(u, v, p) + carving(u, v) + 0.04 * (knot(u, v, [0.2, 0.3]) + knot(u, v, [0.7, 0.55]) + knot(u, v, [0.45, 0.75]))),
);

const twig = shape({ wrap: 'u', ends: ['flat', 'pole'], grid: [48, 160] },
  fold({ axis: path([0.02, 3.0, 0], [0.25, 3.4, 0.05], [0.5, 3.65, 0.1], { interp: 'catmull' }), radius: ({ v }) => 0.045 * sqrt(1 - v) }),
  displace(({ p: [x, y, z] }) => 0.006 * fbm([8 * x, 8 * y, 8 * z], 3)),
);

export default [staff, twig];
