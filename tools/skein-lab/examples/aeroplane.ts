import { shape, lathe, fold, path, rotate, scale, translate, X, Z } from '../src/index.ts';
import type { Op } from '../src/index.ts';

const { PI } = Math;
const body = path([0, 0], [0.18, 0.15], [0.22, 0.9], [0.2, 2.2], [0.08, 3.2], [0, 3.3], { interp: 'catmull', arclength: true });
const airfoil = path([0.5, 0], [0.3, 0.015], [0, 0.03], [-0.4, 0.02], [-0.5, 0], [-0.45, -0.035], [-0.15, -0.06], [0.2, -0.035],
  { closed: true, interp: 'catmull' });

const fuselage = shape({ wrap: 'u', ends: 'pole', grid: [64, 160] }, lathe(body), rotate(Z, -PI / 2));

const wing = (span: number, chord: number, ...place: Op[]) => shape({ wrap: 'u', ends: 'flat', grid: [64, 24] },
  fold({ curve: airfoil }),
  scale(({ v }) => [chord * (1 - 0.45 * v), span, chord * (1 - 0.45 * v)]),
  ...place,
);

export default [
  fuselage,
  wing(1.8, 0.9, rotate(X, PI / 2), translate([1.4, 0, 0])),
  wing(1.8, 0.9, rotate(X, -PI / 2), translate([1.4, 0, 0])),
  wing(0.6, 0.45, rotate(X, PI / 2), translate([2.95, 0.05, 0])),
  wing(0.6, 0.45, rotate(X, -PI / 2), translate([2.95, 0.05, 0])),
  wing(0.6, 0.55, translate([3.0, 0.1, 0])),
];
