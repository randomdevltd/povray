import { shape, scale, fold } from '../src/index.ts';

export default shape({ wrap: 'u', ends: ['flat', 'pole'] },
  scale([1, 1.5, 1]),
  fold({ radius: ({ v }) => 1 - v }),
);
