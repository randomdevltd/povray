import { shape, fold, scale, Y, Z } from '../src/index.ts';

const { sin, abs, PI } = Math;

export default shape({ wrap: 'u', ends: 'pole', grid: [120, 240] },
  fold({ radius: ({ v }) => 0.45 * sin(PI * v) ** 0.8 * (1 + 0.12 * abs(sin(7 * PI * v))) }),
  fold({ axis: Z, along: Y, radius: 1, range: 1.1 * PI, start: -0.55 * PI }),
  scale([1, 1, 0.65]),
);
