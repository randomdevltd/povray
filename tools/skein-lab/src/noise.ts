import type { Vec3 } from './vec.ts';

function hash(x: number, y: number, z: number, k: number): number {
  let h = Math.imul(x, 0x8da6b343) ^ Math.imul(y, 0xd8163841) ^ Math.imul(z, 0xcb1ab31f) ^ Math.imul(k, 0x165667b1);
  h = Math.imul(h ^ (h >>> 15), 0x2c1b3c6d);
  h = Math.imul(h ^ (h >>> 12), 0x297a2d39);
  return ((h ^ (h >>> 15)) >>> 0) / 4294967296;
}

export function cells(p: Vec3): { f1: number; f2: number } {
  const cx = Math.floor(p[0]), cy = Math.floor(p[1]), cz = Math.floor(p[2]);
  let f1 = Infinity, f2 = Infinity;
  for (let i = -1; i <= 1; i++)
    for (let j = -1; j <= 1; j++)
      for (let k = -1; k <= 1; k++) {
        const x = cx + i, y = cy + j, z = cz + k;
        const dx = x + hash(x, y, z, 0) - p[0], dy = y + hash(x, y, z, 1) - p[1], dz = z + hash(x, y, z, 2) - p[2];
        const d = Math.sqrt(dx * dx + dy * dy + dz * dz);
        if (d < f1) [f1, f2] = [d, f1];
        else if (d < f2) f2 = d;
      }
  return { f1, f2 };
}

const smoothstep = (a: number, b: number, x: number) => {
  const t = Math.min(1, Math.max(0, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
};

export function crackle(p: Vec3, width = 0.08): number {
  const { f1, f2 } = cells(p);
  return 1 - smoothstep(0, width, f2 - f1);
}

export function tubeUV(u: number, v: number, length: number, scale = 1): Vec3 {
  const a = 2 * Math.PI * u, r = scale / (2 * Math.PI);
  return [r * Math.cos(a), r * Math.sin(a), scale * length * v];
}

const fade = (t: number) => t * t * t * (t * (t * 6 - 15) + 10);

export function noise(p: Vec3): number {
  const [x, y, z] = p, ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z);
  const fx = fade(x - ix), fy = fade(y - iy), fz = fade(z - iz);
  const at = (i: number, j: number, k: number) => 2 * hash(ix + i, iy + j, iz + k, 3) - 1;
  const mix = (a: number, b: number, t: number) => a + (b - a) * t;
  const plane = (k: number) => mix(mix(at(0, 0, k), at(1, 0, k), fx), mix(at(0, 1, k), at(1, 1, k), fx), fy);
  return mix(plane(0), plane(1), fz);
}

export function fbm(p: Vec3, octaves = 4): number {
  let sum = 0, amp = 0.5, f = 1;
  for (let o = 0; o < octaves; o++, amp /= 2, f *= 2.03) sum += amp * noise([p[0] * f, p[1] * f, p[2] * f]);
  return sum;
}
