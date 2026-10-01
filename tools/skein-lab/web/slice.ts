import { windingSlice } from '../src/index.ts';
import type { Model } from '../src/index.ts';

const colour = (w: number) => (w === 0 ? [21, 23, 28] : w < 0 ? [90, 140, 230] : w === 1 ? [200, 162, 122] : w === 2 ? [235, 140, 60] : [230, 70, 70]);

export function drawSlice(canvas: HTMLCanvasElement, model: Model, n = 200) {
  const sh = Array.isArray(model) ? model[0] : model;
  const mid = (() => {
    let lo = Infinity, hi = -Infinity;
    for (let i = 0; i <= 16; i++) for (let j = 0; j <= 16; j++) { const z = sh.surface(i / 16, j / 16)[2]; lo = Math.min(lo, z); hi = Math.max(hi, z); }
    return (lo + hi) / 2;
  })();
  const s = windingSlice(model, mid, n);
  canvas.width = canvas.height = n;
  const ctx = canvas.getContext('2d')!, img = ctx.createImageData(n, n);
  for (let j = 0; j < n; j++)
    for (let i = 0; i < n; i++) {
      const [r, g, b] = colour(s.w[(n - 1 - j) * n + i]);
      img.data.set([r, g, b, 255], 4 * (j * n + i));
    }
  ctx.putImageData(img, 0, 0);
  return { z: mid, max: Math.max(...s.w), min: Math.min(...s.w) };
}
