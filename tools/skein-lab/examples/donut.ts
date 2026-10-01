import { shape, fold, displace, cells, Y, Z } from '../src/index.ts';
import type { Sample } from '../src/index.ts';

const icing = ({ n }: Sample) => Math.min(1, Math.max(0, (n[2] - 0.1) * 5));
const sprinkles = ({ p }: Sample) => (cells([8 * p[0], 8 * p[1], 8 * p[2]]).f1 < 0.16 ? 0.025 : 0);

export default shape({ wrap: 'uv', grid: [200, 320] },
  fold({ radius: 0.45 }),
  fold({ axis: Z, along: Y, radius: 1.2 }),
  displace((s) => icing(s) * (0.04 + sprinkles(s))),
);
