import { shape, bend } from '../src/index.ts';
import { sheet } from './paper.ts';

export const curl = bend({ origin: [0.6, 0.45, 0], side: [1, 1, 0], radius: 0.07 });

export default shape({ grid: [120, 160] }, sheet, curl);
