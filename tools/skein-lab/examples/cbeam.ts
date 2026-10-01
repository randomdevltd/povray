import { shape, scale, fold, path, by } from '../src/index.ts';

const channel = path(
  [0.5, -0.5], [0.5, -0.3], [-0.25, -0.3], [-0.25, 0.3], [0.5, 0.3], [0.5, 0.5], [-0.5, 0.5], [-0.5, -0.5],
  { closed: true },
);
const flare = path([1, 1, 1], [0.6, 1, 0.6], [1, 1, 1], { interp: 'catmull' });

export default shape({ wrap: 'u', ends: 'flat', grid: [256, 60] },
  scale([1, 2, 1]),
  fold({ curve: channel }),
  scale(by(flare, ({ y }) => y / 2)),
);
