import { shape, fold, torusKnot } from '../src/index.ts';

export default shape({ wrap: 'uv', grid: [48, 1200] },
  fold({ axis: torusKnot(2, 5), radius: 0.12 }),
);
