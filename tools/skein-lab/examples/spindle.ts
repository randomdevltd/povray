import { shape, fold, Y, Z } from '../src/index.ts';

export default shape({ wrap: 'uv' },
  fold({ radius: 1 }),
  fold({ axis: Z, along: Y, radius: 0.6 }),
);
