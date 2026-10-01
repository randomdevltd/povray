import { shape, fold } from '../src/index.ts';
import { trefoil } from './knot.ts';

export default shape({ wrap: 'uv', grid: [64, 640] },
  fold({ axis: trefoil, radius: 0.9 }),
);
