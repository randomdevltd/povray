import { area, buildMesh, seamGap, volume } from './mesh.ts';
import type { Mesh, Shape } from './mesh.ts';
import { selfIntersections } from './intersect.ts';

export * from './vec.ts';
export * from './surface.ts';
export * from './fold.ts';
export * from './frames.ts';
export * from './sections.ts';
export * from './noise.ts';
export * from './mesh.ts';
export * from './intersect.ts';

export interface Metrics {
  area: number;
  volume: number | null;
  seamGap: number;
  selfIntersections: number;
}

export function measure(sh: Shape, m: Mesh = buildMesh(sh)): Metrics {
  return { area: area(m), volume: volume(m), seamGap: seamGap(sh, m), selfIntersections: selfIntersections(sh) };
}
