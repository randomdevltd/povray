import { shape, fold, rotate, series, Y, Z } from '../src/index.ts';
import type { Vec2 } from '../src/index.ts';

const { sin, cos, PI } = Math;
const eight = series((t): Vec2 => [0.6 * sin(2 * PI * t), 0.6 * sin(2 * PI * t) * cos(2 * PI * t)]);

export default shape({ wrap: 'uv', flip: true, grid: [80, 160] },
  fold({ curve: eight }),
  rotate(Y, ({ v }) => PI * v),
  fold({ axis: Z, along: Y, radius: 1.5 }),
);
