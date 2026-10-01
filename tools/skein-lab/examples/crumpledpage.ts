import { shape, translate, cells } from '../src/index.ts';
import { sheet } from './paper.ts';

export default shape({ grid: [160, 200] },
  sheet,
  translate(({ x, y }) => [0, 0, 0.05 * cells([6 * x, 6 * y, 0]).f1]),
);
