import { shape, fold, path, spline, spherical, scale, translate, rotate, X, Y, Z } from '../src/index.ts';
import { digit as finger } from './digit.ts';

const bone = path([0, 3, 0], [0, 1.8, 0.1], [0.05, 1.45, 0.25], [0.15, 0.4, 0.6], { interp: 'catmull', arclength: true });
const muscle = spline([0, 0.24], [0.25, 0.22], [0.45, 0.13], [0.6, 0.16], [1, 0.09]);

const arm = shape({ wrap: 'u', ends: 'flat', grid: [64, 240] }, fold({ axis: bone, radius: muscle }));

const palm = shape({ wrap: 'u', ends: 'pole', grid: [48, 32] },
  spherical(), scale([0.2, 0.26, 0.07]), rotate(X, 0.3), translate([0.16, 0.18, 0.66]),
);

const digit = (dx: number, length: number, splay: number) =>
  finger(length, 0.035, 0.12, rotate(Z, Math.PI + splay), rotate(X, 0.3), translate([0.16 + dx, -0.05, 0.73]));

export default [arm, palm, digit(-0.12, 0.3, -0.1), digit(-0.04, 0.36, -0.03), digit(0.04, 0.34, 0.03), digit(0.12, 0.28, 0.1),
  finger(0.25, 0.04, 0.1, rotate(Z, Math.PI / 2 + 0.6), rotate(Y, -0.4), translate([-0.04, 0.2, 0.62]))];
