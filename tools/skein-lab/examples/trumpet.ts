import { shape, fold, path, spline, scale, translate } from '../src/index.ts';

const tubing = path(
  [-1.8, 0.5, 0], [0.9, 0.5, 0], [1.2, 0.25, 0], [0.9, 0, 0], [-0.5, 0, 0], [-0.8, -0.25, 0],
  [-0.5, -0.5, 0], [0.6, -0.5, 0], [1.4, -0.5, 0], [2.2, -0.5, 0],
  { interp: 'catmull', arclength: true },
);
const bore = spline([0, 0.1], [0.03, 0.045], [0.8, 0.05], [0.9, 0.09], [0.96, 0.2], [1, 0.45]);

const tube = shape({ wrap: 'u', grid: [48, 800] }, fold({ axis: tubing, radius: bore }));

const valve = (x: number) => shape({ wrap: 'u', ends: 'flat', grid: [32, 8] },
  scale([1, 0.9, 1]),
  fold({ radius: 0.09 }),
  translate([x, -0.2, 0]),
);

export default [tube, valve(-0.1), valve(0.15), valve(0.4)];
