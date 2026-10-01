import { shape, fold, ops, Y, Z } from '../src/index.ts';

const { PI } = Math;

const curl = fold({ radius: 0.25, range: 1.5 * PI });
const arc = fold({ axis: Z, along: Y, radius: 1.2, range: 1.25 * PI });

export default shape({}, ops(curl, arc));
