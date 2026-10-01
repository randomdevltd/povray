import { shape, scale, morph, fold, ngon, star } from '../src/index.ts';

export default shape({ wrap: 'u', ends: 'flat', grid: [200, 160] },
  scale([1, 2.5, 1]),
  morph([fold({ radius: 0.5 }), fold({ radius: ngon(4, 0.5) }), fold({ curve: star(5, 0.6, 0.3) })], ({ v }) => v),
);
