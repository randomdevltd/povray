import type { Sample } from './surface.ts';

export type Field<T> = (s: Sample) => T;
export type Key = 'u' | 'v' | 'x' | 'y' | 'z' | 's' | 't' | 'w' | 'theta';

export interface Series<T> {
  (t: number): T;
  (s: Sample): T;
  readonly series: true;
}

export type Value<T> = T | Field<T>;

export const series = <T>(f: (t: number) => T): Series<T> =>
  Object.assign((t: number | Sample) => f(typeof t === 'number' ? t : t.v), { series: true as const }) as Series<T>;

export const isSeries = (x: unknown): x is Series<unknown> =>
  typeof x === 'function' && (x as { series?: boolean }).series === true;

const TAU = 2 * Math.PI;

function input(k: Key): (s: Sample) => number {
  if (k === 'theta') return (s) => (s as unknown as { theta: number }).theta / TAU;
  return (s) => (s as unknown as Record<Key, number>)[k];
}

export function by<T, S extends Sample = Sample>(f: (t: number) => T, map: Key | ((s: S) => number)): (s: S) => T {
  const m = typeof map === 'string' ? input(map) : map;
  return (s) => f(m(s));
}

export function field<T>(x: Value<T>): Field<T> {
  if (typeof x === 'function') return x as Field<T>;
  return () => x;
}

export const isConstant = <T>(x: Value<T>): x is T => typeof x !== 'function';
