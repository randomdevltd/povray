import { shape, lathe, path } from '../src/index.ts';

const section = path(
  [0, 0], [0.5, 0.02], [0.9, 0.18], [1.15, 0.5], [1.25, 0.8], [1.2, 0.86],
  [1.12, 0.8], [1.02, 0.52], [0.8, 0.26], [0.45, 0.14], [0, 0.12],
  { interp: 'catmull', arclength: true },
);

export default shape({ wrap: 'u', ends: 'pole', grid: [160, 240] }, lathe(section));
