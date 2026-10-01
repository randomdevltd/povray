import { shape, fold, path, rotate, translate, X } from '../src/index.ts';
import type { Op } from '../src/index.ts';

const { PI } = Math;
const oval = path([-0.5, 0.5, 0], [0.5, 0.5, 0], [1, 0, 0], [0.5, -0.5, 0], [-0.5, -0.5, 0], [-1, 0, 0],
  { closed: true, interp: 'catmull', arclength: true });

const link = (...place: Op[]) => shape({ wrap: 'uv', grid: [32, 240] }, fold({ axis: oval, radius: 0.12 }), ...place);

export default [link(), link(rotate(X, PI / 2), translate([1.4, 0, 0])), link(translate([2.8, 0, 0]))];
