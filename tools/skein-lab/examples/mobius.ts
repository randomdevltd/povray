import { shape, translate, scale, rotate, fold, Y, Z } from '../src/index.ts';

const { PI } = Math;

export default shape({ wrap: 'v', flip: true, grid: [24, 240] },
  translate([-0.5, 0, 0]),
  scale([0.6, 1, 1]),
  rotate(Y, ({ v }) => PI * v),
  fold({ axis: Z, along: Y, radius: 1.2 }),
);
