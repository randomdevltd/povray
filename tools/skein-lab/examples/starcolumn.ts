import { shape, scale, fold, star } from '../src/index.ts';

export default shape({ wrap: 'u', ends: 'flat', grid: [200, 120] },
  scale([1, 2.5, 1]),
  fold({ curve: star(5, 0.6, 0.3), twist: ({ v }) => 0.8 * v }),
);
