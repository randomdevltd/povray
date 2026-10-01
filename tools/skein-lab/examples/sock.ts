import { shape, fold, path, spline, displace } from '../src/index.ts';

const { cos, sqrt, min, PI } = Math;
const leg = path([0, 1.8, 0], [0, 1.0, 0], [0, 0.55, 0], [0.12, 0.2, 0], [0.5, 0.05, 0], [1.3, 0.05, 0],
  { interp: 'catmull', arclength: true });
const girth = spline([0, 0.24], [0.3, 0.22], [0.5, 0.2], [0.62, 0.23], [0.75, 0.2], [1, 0.19]);

export default shape({ wrap: 'u', ends: ['flat', 'pole'], grid: [96, 320] },
  fold({ axis: leg, radius: ({ v }) => girth(v) * sqrt(min(1, 10 * (1 - v))) }),
  displace(({ u, v }) => (v < 0.12 ? 0.008 * cos(2 * PI * 40 * u) : 0)),
);
