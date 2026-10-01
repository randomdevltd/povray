import { shape, scale, fold } from '../src/index.ts';

export default shape({ wrap: 'u', ends: 'flat' },
  scale([1, 2, 1]),
  fold({ radius: 0.5 }),
);
