import { shape, rotate, fold, scale, X } from '../src/index.ts';

const { PI } = Math;

const ramp = shape({ grid: [320, 24] },
  rotate(X, PI / 2),
  fold({ range: 4 * PI, radius: 0.3, axial: ({ theta }) => 0.12 * theta }),
);

const post = shape({ wrap: 'u', ends: 'flat' }, scale([1, 1.6, 1]), fold({ radius: 0.28 }));

export default [ramp, post];
