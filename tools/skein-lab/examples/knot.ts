import { shape, fold } from '../src/index.ts';
import type { Vec3 } from '../src/index.ts';

const { sin, cos, PI } = Math;

export const trefoil = (t: number): Vec3 => {
  const a = 2 * PI * t, r = 2 + cos(3 * a);
  return [r * cos(2 * a), r * sin(2 * a), -sin(3 * a)];
};

export default shape({ wrap: 'uv', grid: [48, 960] },
  fold({ axis: trefoil, radius: 0.05 }),
);
