import { shape, lathe, fold, scale, translate } from '../src/index.ts';

const { sin, cos, PI } = Math;

const body = shape({ wrap: 'u', ends: 'pole', grid: [240, 120] },
  lathe(({ v, theta }) => [sin(PI * v) * (1 + 0.07 * cos(10 * theta)), -0.75 * cos(PI * v)]),
);

const stem = shape({ wrap: 'u', ends: 'flat' },
  scale([1, 0.35, 1]),
  fold({ radius: ({ v }) => 0.09 - 0.03 * v, twist: ({ v }) => 2 * v }),
  translate([0, 0.752, 0]),
);

export default [body, stem];
