import { shape, lathe, spline } from '../src/index.ts';

const profile = spline(
  [0, [0, 0]], [0.12, [0.55, 0]], [0.16, [0.58, 0.03]], [0.24, [0.12, 0.08]], [0.3, [0.05, 0.2]],
  [0.55, [0.045, 0.9]], [0.62, [0.12, 1.1]], [0.74, [0.42, 1.3]], [0.88, [0.56, 1.7]], [1, [0.52, 2.1]],
);

export default shape({ wrap: 'u', ends: ['pole', 'open'], grid: [128, 320] }, lathe(profile));
