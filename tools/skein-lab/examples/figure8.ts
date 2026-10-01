import { shape, fold, series } from '../src/index.ts';
import type { Vec3 } from '../src/index.ts';

const { sin, cos, PI } = Math;
const loop = series((t): Vec3 => [1.5 * sin(2 * PI * t), 0.75 * sin(4 * PI * t), 0.3 * cos(2 * PI * t)]);

export default shape({ wrap: 'uv', grid: [48, 480] },
  fold({ axis: loop, radius: 0.12 }),
);
