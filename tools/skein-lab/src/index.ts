import { area, buildMesh, parts, seamGap, volume } from './mesh.ts';
import type { Model } from './mesh.ts';
import { intersections } from './intersect.ts';
import { orientationOf, unionVolume } from './winding.ts';

export { X, Y, Z, add, sub, dot, cross, norm, dist, normalize, lerp } from './vec.ts';
export type { Vec3 } from './vec.ts';
export { Sample, ops, fn, displace, spherical, morph, normalAt, unitQuad } from './surface.ts';
export type { Surface, Op } from './surface.ts';
export { series, by, field, isSeries } from './value.ts';
export type { Field, Value, Series, Key } from './value.ts';
export { path, spline } from './series.ts';
export type { Vec2, PathOptions, Interp, Knot } from './series.ts';
export { fold, lathe, FoldSample } from './fold.ts';
export type { FoldOptions, FoldValue, Cyl, Curve } from './fold.ts';
export { scale, translate, rotate, matrix, applyMatrix, bend } from './transform.ts';
export type { Matrix, BendOptions } from './transform.ts';
export { circle, ngon, superellipse, star } from './sections.ts';
export { helix, torusKnot } from './paths.ts';
export { rmf, pathLength, tangent } from './frames.ts';
export { cells, crackle, tubeUV, noise, fbm } from './noise.ts';
export { RayField, unionVolume, windingSlice, orientation, orientationOf } from './winding.ts';
export type { Orientation } from './winding.ts';
export { shape, buildMesh, area, volume, seamGap, parts, merge } from './mesh.ts';
export type { Shape, Model, Mesh, Topology, End } from './mesh.ts';
export { selfIntersections, contacts, intersections, countSelfIntersections, intersectingPairs } from './intersect.ts';

export interface Metrics {
  area: number;
  volume: number | null;
  seamGap: number;
  selfIntersections: number;
  contacts: number;
  union: number | null;
  orientable: boolean;
  open: boolean;
}

export function measure(model: Model): Metrics {
  let a = 0, v: number | null = 0, gap = 0;
  for (const sh of parts(model)) {
    const m = buildMesh(sh, undefined, undefined, false);
    a += area(m);
    const vol = volume(m);
    v = v === null || vol === null ? null : v + vol;
    gap = Math.max(gap, seamGap(sh, m));
  }
  const { self, contacts } = intersections(model);
  const o = orientationOf(model), orientable = o.every((x) => x.orientable), open = o.some((x) => x.boundaryEdges > 0);
  const union = orientable && !open ? unionVolume(model).union : null;
  return { area: a, volume: v, seamGap: gap, selfIntersections: self, contacts, union, orientable, open };
}
