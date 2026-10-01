import { shape, fold, scale, Y, Z } from '../src/index.ts';

const { exp, PI } = Math;
const range = 5 * PI, grow = (theta: number) => exp(0.17 * (theta - range));

export default shape({ wrap: 'u', ends: 'flat', grid: [96, 600] },
  fold({ radius: 0.5 }),
  scale(({ v }) => [grow(v * range), 1, grow(v * range)]),
  fold({ axis: Z, along: Y, range, radius: ({ theta }) => 0.9 * grow(theta), axial: ({ theta }) => -0.6 * grow(theta) }),
);
