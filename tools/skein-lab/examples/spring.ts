import { shape, fold, Y, Z } from '../src/index.ts';

const { PI } = Math;

export default shape({ wrap: 'u', ends: 'flat', grid: [32, 960] },
  fold({ radius: 0.08 }),
  fold({ axis: Z, along: Y, radius: 0.6, range: 6 * 2 * PI, axial: ({ theta }) => 0.05 * theta }),
);
