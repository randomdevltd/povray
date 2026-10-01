import { shape, spherical, scale, displace, fbm, cells } from '../src/index.ts';

export default shape({ wrap: 'u', ends: 'pole', grid: [240, 160] },
  spherical(),
  scale([1.25, 0.8, 1]),
  displace(({ p: [x, y, z] }) => 0.3 * fbm([1.3 * x, 1.3 * y, 1.3 * z], 5) - 0.12 * cells([2 * x, 2 * y, 2 * z]).f1),
);
