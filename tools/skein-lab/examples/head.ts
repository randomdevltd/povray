import { shape, spherical, scale, displace, translate, rotate, fold, Y } from '../src/index.ts';

const { exp, max } = Math;
const bump = (x: number, y: number, z: number, at: number[], w: number) =>
  exp(-((x - at[0]) ** 2 + (y - at[1]) ** 2 + (z - at[2]) ** 2) / w);

const skull = shape({ wrap: 'u', ends: 'pole', grid: [160, 160] },
  spherical(),
  scale(({ y }) => [0.78 - 0.12 * max(0, -y), 1, 0.9 - 0.1 * max(0, -y)]),
  displace(({ p: [x, y, z] }) => 0.16 * bump(x, y, z, [0, -0.1, 0.92], 0.012) - 0.06 * bump(x, y, z, [0.3, 0.15, 0.78], 0.02)
    - 0.06 * bump(x, y, z, [-0.3, 0.15, 0.78], 0.02) + 0.04 * bump(x, y, z, [0, -0.55, 0.7], 0.03)),
);

const ear = (side: number) => shape({ wrap: 'u', ends: 'pole', grid: [32, 24] },
  spherical(), scale([0.04, 0.2, 0.12]), rotate(Y, side * 0.3), translate([side * 0.8, 0.02, -0.05]),
);

const eye = (side: number) => shape({ wrap: 'u', ends: 'pole', grid: [24, 16] }, spherical(0.07), translate([side * 0.27, 0.15, 0.74]));

const neck = shape({ wrap: 'u', ends: 'flat', grid: [48, 8] }, scale([1, 0.7, 1]), fold({ radius: 0.33 }), translate([0, -1.45, -0.05]));

export default [skull, ear(1), ear(-1), eye(1), eye(-1), neck];
