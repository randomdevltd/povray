import { shape, scale, fold, displace, crackle, tubeUV } from '../src/index.ts';

const { PI } = Math;

export default shape({ wrap: 'u', ends: 'flat', grid: [240, 480] },
  scale([1, 4, 1]),
  fold({ radius: ({ v }) => 0.5 - 0.15 * v, twist: ({ v }) => 1.5 * PI * v }),
  displace(({ u, v }) => -0.04 * crackle(tubeUV(u, v, 0.45, 10), 0.12)),
);
