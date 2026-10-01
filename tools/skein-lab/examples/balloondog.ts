import { shape, fold, path } from '../src/index.ts';
import type { Vec3 } from '../src/index.ts';

const { sqrt } = Math;

const sausage = (a: Vec3, b: Vec3, r: number) => shape({ wrap: 'u', ends: 'pole', grid: [32, 48] },
  fold({ axis: path(a, b), radius: ({ v }) => 2 * r * sqrt(v * (1 - v)) }),
);

export default [
  sausage([-0.65, 0.8, 0], [0.65, 0.8, 0], 0.17),
  sausage([0.6, 0.78, 0.12], [0.62, 0, 0.22], 0.11), sausage([0.6, 0.78, -0.12], [0.62, 0, -0.22], 0.11),
  sausage([-0.6, 0.78, 0.12], [-0.62, 0, 0.22], 0.11), sausage([-0.6, 0.78, -0.12], [-0.62, 0, -0.22], 0.11),
  sausage([0.68, 0.85, 0], [0.82, 1.45, 0], 0.12),
  sausage([0.8, 1.5, 0], [1.35, 1.38, 0], 0.13),
  sausage([0.78, 1.55, 0.08], [0.6, 1.95, 0.2], 0.09), sausage([0.78, 1.55, -0.08], [0.6, 1.95, -0.2], 0.09),
  sausage([-0.68, 0.85, 0], [-0.95, 1.35, 0], 0.08),
];
