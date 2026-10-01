import { shape, translate, scale, rotate, Y } from '../src/index.ts';

const { PI } = Math;

export default shape({ grid: [24, 240] },
  translate([-0.5, 0, 0]),
  scale([0.6, 3, 1]),
  rotate(Y, ({ y }) => 1.2 * PI * y),
);
