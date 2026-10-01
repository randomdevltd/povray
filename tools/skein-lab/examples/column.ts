import { shape, scale, fold, ngon } from '../src/index.ts';

const { PI } = Math;

export default shape({ wrap: 'u', ends: 'flat', grid: [240, 120] },
  scale([1, 3, 1]),
  fold({ radius: ngon(6, 0.5), twist: ({ v }) => (PI / 3) * v }),
);
