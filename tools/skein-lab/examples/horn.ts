import { shape, fold, Y, Z } from '../src/index.ts';

const { exp, PI } = Math;

export default shape({ wrap: 'u', ends: ['flat', 'pole'], grid: [96, 320] },
  fold({ radius: ({ v }) => 0.35 * (1 - v) }),
  fold({ axis: Z, along: Y, range: 1.6 * PI, radius: ({ theta }) => 1.5 * exp(-0.22 * theta), axial: ({ theta }) => 0.12 * theta }),
);
