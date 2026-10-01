import { shape, lathe, path, scale, translate, fold, X, Y } from '../src/index.ts';

const { abs, cos, PI } = Math;
const outline = path([0, 0], [0.32, 0.4], [0.3, 1.1], [0.12, 1.7], [0, 2], { interp: 'catmull' });

export default shape({ wrap: 'u', ends: 'pole', grid: [160, 200] },
  lathe(outline),
  scale([1, 1, 0.03]),
  translate(({ x, y }) => [0, 0, 0.25 * abs(x) + 0.006 * cos(PI * (12 * abs(x) + 4 * y))]),
  fold({ axis: X, along: Y, radius: 3, range: 0.35 }),
);
