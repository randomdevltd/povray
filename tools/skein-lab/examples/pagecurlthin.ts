import { shape } from '../src/index.ts';
import { slab } from './paper.ts';
import { curl } from './pagecurl.ts';

export default shape({ wrap: 'u', ends: 'flat', grid: [240, 160] }, slab, curl);
