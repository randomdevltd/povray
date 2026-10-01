import { shape, fold, path, ngon, scale, translate, Y, Z } from '../src/index.ts';

const { PI } = Math;
const molding = path([0, 0], [0.18, 0], [0.18, 0.05], [0.12, 0.09], [0.04, 0.1], [0, 0.07], { closed: true });
const square = path([1, 1], [-1, 1], [-1, -1], [1, -1], { closed: true });

const frame = shape({ wrap: 'uv', grid: [64, 256] },
  fold({ curve: molding }),
  fold({ axis: Z, along: Y, curve: square }),
);

const canvas = shape({ wrap: 'u', ends: 'flat', grid: [80, 4] },
  fold({ radius: ngon(4, Math.SQRT1_2), twist: PI / 4 }),
  scale([1.96, 1.96, 0.01]),
  translate([0, -0.98, -0.02]),
);

export default [frame, canvas];
