import { shape, spherical, displace, cells, fbm } from '../src/index.ts';

export default shape({ wrap: 'u', ends: 'pole', grid: [240, 160] },
  spherical(),
  displace(({ p: [x, y, z] }) => 0.1 * fbm([2 * x, 2 * y, 2 * z]) - 0.22 * cells([3 * x, 3 * y, 3 * z]).f1),
);
