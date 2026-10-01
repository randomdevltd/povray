import { shape, lathe, path, ngon } from '../src/index.ts';

const { PI } = Math;
const square = ngon(4, Math.SQRT2);
const wall = path([0, 0], [0.5, 0], [0.5, 0.6], [0.46, 0.6], [0.46, 0.04], [0, 0.04]);

export default shape({ wrap: 'u', ends: 'pole', grid: [160, 200] },
  lathe(({ v, theta }) => [wall(v)[0] * square({ theta }), wall(v)[1]], { twist: PI / 4 }),
);
