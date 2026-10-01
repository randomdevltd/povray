import { ops, translate, scale, fold, ngon } from '../src/index.ts';

const { PI } = Math;
export const page = [0.85, 1.1] as const;

export const sheet = ops(translate([-0.5, 0, 0]), scale([page[0], page[1], 1]));

export const slab = ops(fold({ radius: ngon(4, Math.SQRT1_2), twist: PI / 4 }), scale([page[0], page[1], 0.008]));
