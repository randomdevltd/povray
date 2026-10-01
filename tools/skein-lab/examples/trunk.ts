import { shape, fold, displace, path, spline, fbm, crackle } from '../src/index.ts';
import type { Vec3 } from '../src/index.ts';

const { abs, sin, cos, min, PI } = Math;

const spine = path([0, 2.6, 0], [0, 1.6, 0.15], [0.1, 0.6, 0.3], [0.2, 0.05, 0.6], [0.25, 0.1, 1.0], [0.2, 0.45, 1.12],
  { interp: 'catmull', arclength: true });
const girth = spline([0, 0.36], [0.5, 0.22], [0.85, 0.13], [0.93, 0.12], [0.97, 0.16], [1, 0.17]);

const rings = (u: number, v: number) => -0.025 * (1 - v * 0.6) * (0.6 + 0.4 * cos(2 * PI * u)) * abs(sin(PI * (40 * v + 30 * v * v)));
const skin = ([x, y, z]: Vec3) =>
  -0.006 * crackle([25 * x, 25 * y, 25 * z], 0.15) + 0.004 * fbm([60 * x, 60 * y, 60 * z], 2);

export default shape({ wrap: 'u', ends: 'flat' },
  fold({ axis: spine, radius: girth }),
  displace(({ u, v, p }) => min(1, 40 * v, 40 * (1 - v)) * (rings(u, v) + skin(p))),
);
