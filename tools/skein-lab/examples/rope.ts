import { shape, fold, helix, rotate, Y } from '../src/index.ts';

const { PI } = Math;

const strand = (k: number) => shape({ wrap: 'u', ends: 'flat', grid: [32, 480] },
  fold({ axis: helix(0.2, 3, 2.5), radius: 0.12 }),
  rotate(Y, (2 * PI * k) / 3),
);

export default [0, 1, 2].map(strand);
