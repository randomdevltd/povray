import { shape, lathe, path, displace, fold, translate, Y, Z } from '../src/index.ts';

const { sin, PI } = Math;
const wall = path([0, 0], [0.7, 0], [0.85, 0.15], [0.95, 0.7], [0.92, 0.72], [0.82, 0.18], [0.67, 0.04], [0, 0.04],
  { interp: 'catmull', arclength: true });

const body = shape({ wrap: 'u', ends: 'pole', grid: [320, 240] },
  lathe(wall),
  displace(({ u, v }) => 0.012 * sin(2 * PI * 36 * u) * sin(2 * PI * 40 * v)),
);

const handle = shape({ wrap: 'u', ends: 'flat', grid: [24, 120] },
  fold({ radius: 0.035 }),
  fold({ axis: Z, along: Y, radius: 0.93, range: PI, start: -PI / 2 }),
  displace(({ v }) => 0.008 * sin(2 * PI * 30 * v)),
  translate([0, 0.72, 0]),
);

export default [body, handle];
