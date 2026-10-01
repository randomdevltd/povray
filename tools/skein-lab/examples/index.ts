import sphere from './sphere.ts';
import torus from './torus.ts';
import frustum from './frustum.ts';
import log from './log.ts';
import knot from './knot.ts';
import curl from './curl.ts';
import fatknot from './fatknot.ts';
import column from './column.ts';

export const examples = { sphere, torus, frustum, log, knot, curl, column, fatknot };
export type ExampleName = keyof typeof examples;
