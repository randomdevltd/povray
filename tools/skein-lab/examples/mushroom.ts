import { shape, lathe, path } from '../src/index.ts';

const profile = path(
  [0, 0], [0.18, 0], [0.2, 0.05], [0.16, 0.5], [0.17, 0.75], [0.5, 0.84], [0.9, 0.84],
  [1.0, 0.92], [0.85, 1.15], [0.45, 1.38], [0, 1.45],
  { interp: 'catmull', arclength: true },
);

export default shape({ wrap: 'u', ends: 'pole', grid: [128, 240] }, lathe(profile));
