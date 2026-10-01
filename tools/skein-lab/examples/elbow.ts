import { shape, fold, Y, Z } from '../src/index.ts';

const { PI } = Math;

export default shape({ wrap: 'u', ends: 'flat' },
  fold({ radius: 0.25 }),
  fold({ axis: Z, along: Y, radius: 1, range: PI / 2 }),
);
