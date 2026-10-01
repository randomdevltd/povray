import { shape, lathe, path, translate, displace, bend, fbm, cells, X, Y } from '../src/index.ts';
import type { Vec3 } from '../src/index.ts';

const { abs, max, min, hypot } = Math;

const profile = path([0, 0], [1.4, 0], [1.43, 0.03], [1.38, 0.05], [0.55, 0.07], [0.42, 0.6], [0.18, 1.6], [0.06, 2.3], [0, 2.4],
  { interp: 'linear' });
const crease = (d: number) => -0.05 * max(0, 1 - abs(d) / 0.05);
const crumple = ([x, y, z]: Vec3) => 0.02 * fbm([3 * x, 3 * y, 3 * z], 3) - 0.015 * cells([7 * x, 7 * y, 7 * z]).f1;

export default shape({ wrap: 'u', ends: 'pole', grid: [200, 320] },
  lathe(profile),
  translate(({ x, z }) => [0, -0.06 * hypot(x, z) ** 2 * (1 + fbm([1.5 * x, 0, 1.5 * z])), 0]),
  displace(({ p: [x, y, z] }) =>
    min(1, hypot(x, z) / 0.4) * min(1, max(0, y - 0.1) / 0.2) * (crumple([x, y, z]) + crease(x + 0.4 * y - 0.9) + crease(z - 0.3 * y + 0.2))),
  bend({ origin: [0, 1.5, 0], side: Y, toward: X, radius: 0.35, angle: 1.7 }),
);
