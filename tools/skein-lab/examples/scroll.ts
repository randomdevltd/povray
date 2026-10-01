import { shape, scale, fold } from '../src/index.ts';

const { PI } = Math;

export default shape({ grid: [320, 80] },
  scale([1, 2, 1]),
  fold({ range: 5 * PI, radius: ({ theta }) => 0.15 + 0.07 * theta }),
);
