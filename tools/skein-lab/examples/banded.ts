import { shape, scale, fold, displace, path, by } from '../src/index.ts';

const ridges = path(0, 0.06, 0, 0.025, 0, 0.06, 0, 0.025, { interp: 'catmull', closed: true });

export default shape({ wrap: 'u', ends: 'flat', grid: [256, 80] },
  scale([1, 1.5, 1]),
  fold({ radius: 0.6 }),
  displace(by(ridges, 'u')),
);
