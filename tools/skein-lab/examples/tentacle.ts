import { shape, fold, path, spline } from '../src/index.ts';

const spine = path([0, 0, 0], [0.3, 1, 0.2], [-0.2, 2, 0.5], [0.4, 2.8, 0.2], [1.1, 3.1, -0.3], { interp: 'cubic' });
const taper = spline([0, 0.28], [0.3, 0.22], [0.8, 0.07], [1, 0.015]);

export default shape({ wrap: 'u', ends: 'flat', grid: [64, 320] },
  fold({ axis: spine, radius: taper }),
);
