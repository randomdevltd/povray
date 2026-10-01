import { shape, fn } from '../src/index.ts';

const { sin, cos, PI } = Math;

export default shape({ wrap: 'u', ends: 'pole' },
  fn(({ u, v }) => {
    const theta = 2 * PI * u, phi = PI * v;
    return [sin(phi) * cos(theta), -cos(phi), -sin(phi) * sin(theta)];
  }),
);
