import { shape, fold, path, spline, scale, displace, Y } from '../src/index.ts';

const { sqrt, min, max, abs } = Math;
const girth = spline([0, 0.15], [0.45, 0.15], [0.8, 0.18], [1, 0.17]);
const plate = (d: number, half: number) => min(1, max(0, (half - abs(d)) / 0.02));
const nail = (u: number, v: number) => 0.014 * plate(u > 0.5 ? u - 1 : u, 0.11) * plate(v - 0.77, 0.14);

export default shape({ wrap: 'u', ends: ['flat', 'pole'], grid: [160, 160] },
  fold({
    axis: path([0, 0, 0], [0, 0.02, 0.22], [0, 0.07, 0.42], { interp: 'catmull' }),
    up: Y,
    radius: ({ v }) => girth(v) * sqrt(min(1, 4 * (1 - v))),
  }),
  displace(({ u, v }) => nail(u, v)),
  scale([1, 0.72, 1]),
);
