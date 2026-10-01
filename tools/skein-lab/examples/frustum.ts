import { shape, fn, fold } from '../src/index.ts';

export default shape({ wrap: 'u', ends: 'flat' },
  fn(({ x, y, z }) => [x, 2 * y, z]),
  fold({ radius: ({ v }) => 1 - 0.75 * v }),
);
