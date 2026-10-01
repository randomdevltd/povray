import { shape, bend, X } from '../src/index.ts';
import { sheet } from './paper.ts';

export default shape({ grid: [120, 120] },
  sheet,
  bend({ origin: [-0.425, 0, 0], side: X, radius: ({ v }) => 0.2 + 0.25 * v, angle: 2.1 }),
);
