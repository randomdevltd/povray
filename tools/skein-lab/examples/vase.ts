import { shape, scale, fold, path } from '../src/index.ts';

const profile = path(0.35, 0.55, 0.6, 0.42, 0.22, 0.26, 0.38, { interp: 'cubic' });

export default shape({ wrap: 'u', ends: ['flat', 'open'], grid: [160, 200] },
  scale([1, 2.2, 1]),
  fold({ radius: profile }),
);
