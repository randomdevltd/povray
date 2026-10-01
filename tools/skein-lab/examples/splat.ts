import { shape, spherical, scale, translate, path } from '../src/index.ts';

const outline = path(1, 0.7, 1.35, 0.8, 1.1, 0.65, 1.5, 0.75, 1.05, 0.7, 1.25, 0.8, { closed: true, interp: 'catmull' });

const puddle = shape({ wrap: 'u', ends: 'pole', grid: [240, 64] },
  spherical(),
  scale(({ u }) => [outline(u), 0.06, outline(u)]),
);

const drop = (x: number, z: number, r: number) =>
  shape({ wrap: 'u', ends: 'pole', grid: [24, 16] }, spherical(r), scale([1, 0.5, 1]), translate([x, 0, z]));

export default [puddle, drop(1.75, 0.3, 0.08), drop(-0.4, 1.7, 0.06), drop(-1.6, -0.7, 0.1), drop(0.6, -1.5, 0.05)];
