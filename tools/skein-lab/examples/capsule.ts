import { shape, lathe } from '../src/index.ts';
import type { Vec2 } from '../src/index.ts';

const { sin, cos, PI } = Math;
const r = 0.4, h = 0.6;

const profile = ({ v }: { v: number }): Vec2 => {
  const k = 3 * v, a = (PI / 2) * Math.min(k, Math.max(1, k - 1));
  if (k < 1) return [r * sin(a), -h - r * cos(a)];
  if (k > 2) return [r * sin(a), h - r * cos(a)];
  return [r, h * (2 * k - 3)];
};

export default shape({ wrap: 'u', ends: 'pole' }, lathe(profile));
