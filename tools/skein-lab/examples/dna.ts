import { shape, fold, helix, rotate, scale, translate, Y, Z } from '../src/index.ts';

const { PI } = Math;
const R = 0.6, H = 4, turns = 1.5;

const backbone = (phase: number) => shape({ wrap: 'u', ends: 'flat', grid: [32, 480] },
  fold({ axis: helix(R, H, turns), radius: 0.08 }),
  rotate(Y, phase),
);

const rung = (y: number) => shape({ wrap: 'u', ends: 'flat', grid: [24, 8] },
  fold({ radius: 0.035 }),
  translate([0, -0.5, 0]),
  scale([1, 2 * (R - 0.1), 1]),
  rotate(Z, -PI / 2),
  rotate(Y, (2 * PI * turns * y) / H),
  translate([0, y, 0]),
);

export default [backbone(0), backbone(PI), ...Array.from({ length: 14 }, (_, i) => rung(0.15 + i * 0.27))];
