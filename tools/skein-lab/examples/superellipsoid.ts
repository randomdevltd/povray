import { shape, spherical, scale } from '../src/index.ts';

const p = 4, { abs } = Math;

export default shape({ wrap: 'u', ends: 'pole' },
  spherical(),
  scale(({ x, y, z }) => (abs(x) ** p + abs(y) ** p + abs(z) ** p) ** (-1 / p)),
);
