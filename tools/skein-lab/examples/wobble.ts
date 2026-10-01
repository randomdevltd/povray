import { shape, scale, fold, translate, path } from '../src/index.ts';

const sway = path([0, 0, 0], [0.35, 0, 0.1], [-0.25, 0, 0.3], [0.2, 0, -0.2], [0, 0, 0], { interp: 'catmull' });

export default shape({ wrap: 'u', ends: 'flat', grid: [96, 200] },
  scale([1, 3, 1]),
  fold({ radius: 0.3 }),
  translate(sway),
);
