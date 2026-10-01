import { shape, fn, fold, ngon } from '../src/index.ts';

const { PI } = Math;
const hex = ngon(6, 0.5);

export default shape({ wrap: 'u', ends: 'flat', grid: [240, 120] },
  fn(({ x, y, z }) => [x, 3 * y, z]),
  fold({ radius: (c) => [hex(c), (PI / 3) * c.v, 0] }),
);
