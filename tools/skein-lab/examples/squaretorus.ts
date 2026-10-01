import { shape, fold, ngon, Y, Z } from '../src/index.ts';

const { PI } = Math;

export default shape({ wrap: 'uv', shift: 0.25, grid: [160, 240] },
  fold({ radius: ngon(4, 0.5), twist: ({ v }) => (PI / 2) * v }),
  fold({ axis: Z, along: Y, radius: 2 }),
);
