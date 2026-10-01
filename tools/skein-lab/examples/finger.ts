import { shape, fold, path, spline } from '../src/index.ts';
import type { Op } from '../src/index.ts';

const { sqrt, min } = Math;
const knuckles = spline([0, 1], [0.2, 0.92], [0.42, 1.0], [0.55, 0.86], [0.72, 0.92], [0.85, 0.82], [1, 0.8]);

export const finger = (length: number, radius: number, bend: number, ...place: Op[]) => shape(
  { wrap: 'u', ends: ['flat', 'pole'], grid: [48, 120] },
  fold({
    axis: path([0, 0, 0], [0, 0.45 * length, 0.1 * bend], [0, 0.8 * length, 0.4 * bend], [0, length, 0.85 * bend], { interp: 'catmull' }),
    radius: ({ v }) => radius * knuckles(v) * sqrt(min(1, 6 * (1 - v))),
  }),
  ...place,
);

export default finger(1, 0.1, 0.35);
