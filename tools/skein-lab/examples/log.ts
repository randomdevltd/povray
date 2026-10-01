import { shape, fn, fold, displace, crackle, tubeUV } from '../src/index.ts';

const { PI } = Math;

export default shape({ wrap: 'u', ends: 'flat' },
  fn(({ x, y, z }) => [x, 4 * y, z]),
  fold({ radius: ({ v }) => [0.5 - 0.15 * v, 1.5 * PI * v, 0] }),
  displace(({ u, v }) => -0.04 * crackle(tubeUV(u, v, 1.2, 9), 0.12)),
);
