import { shape, translate, scale, spline, by } from '../src/index.ts';

const fold = spline(
  [0, [0, 0, 0.14]], [0.42, [0, 0, 0]], [0.5, [0, 0, -0.22]], [0.58, [0, 0, 0]], [1, [0, 0, 0.14]],
  { interp: 'linear' },
);

export default shape({ grid: [200, 80] },
  translate([-0.5, 0, 0]),
  translate(by(fold, 'u')),
  scale(({ v }) => [1.6 * (1 - 0.9 * v), 2, 1 - 0.8 * v]),
);
