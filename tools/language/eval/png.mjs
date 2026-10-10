// SPDX-License-Identifier: AGPL-3.0-or-later
// Minimal PNG codec on node:zlib: decodes non-interlaced grey, RGB, palette and alpha images to RGB8 plus alpha.
import { crc32, deflateSync, inflateSync } from 'node:zlib';

const SIGNATURE = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
const CHANNELS = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 };
const DEPTHS = { 0: [1, 2, 4, 8, 16], 2: [8, 16], 3: [1, 2, 4, 8], 4: [8, 16], 6: [8, 16] };

export function decodePng(buffer) {
  if (buffer.length < 8 || !buffer.subarray(0, 8).equals(SIGNATURE)) throw new Error('not a PNG file');
  let header = null;
  let palette = null;
  const data = [];
  let ended = false;
  for (let pos = 8; pos < buffer.length && !ended;) {
    if (pos + 12 > buffer.length) throw new Error('truncated chunk header');
    const length = buffer.readUInt32BE(pos);
    if (pos + 12 + length > buffer.length) throw new Error('truncated chunk');
    const type = buffer.toString('latin1', pos + 4, pos + 8);
    const chunk = buffer.subarray(pos + 8, pos + 8 + length);
    if ((crc32(buffer.subarray(pos + 4, pos + 8 + length)) >>> 0) !== buffer.readUInt32BE(pos + 8 + length))
      throw new Error(`bad CRC in ${type}`);
    if (type === 'IHDR') header = { width: chunk.readUInt32BE(0), height: chunk.readUInt32BE(4), depth: chunk[8],
      colour: chunk[9], interlace: chunk[12] };
    else if (type === 'PLTE') palette = chunk;
    else if (type === 'IDAT') data.push(chunk);
    else if (type === 'IEND') ended = true;
    pos += length + 12;
  }
  if (!header || !ended) throw new Error('missing IHDR or IEND');
  const { width, height, depth, colour, interlace } = header;
  if (!width || !height) throw new Error('image has no pixels');
  if (!DEPTHS[colour]?.includes(depth)) throw new Error(`colour type ${colour} with depth ${depth} is not valid`);
  if (interlace) throw new Error('interlaced PNG is not supported');
  if (colour === 3 && !palette) throw new Error('palette image without PLTE');
  const raw = inflateSync(Buffer.concat(data));
  const channels = CHANNELS[colour];
  const bytesPerPixel = Math.max(1, (channels * depth) >> 3);
  const stride = Math.ceil((width * channels * depth) / 8);
  if (raw.length < (stride + 1) * height) throw new Error('image data is shorter than the image');
  const rows = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    if (filter > 4) throw new Error(`unknown filter type ${filter}`);
    const line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    const out = rows.subarray(y * stride, (y + 1) * stride);
    const up = y ? rows.subarray((y - 1) * stride, y * stride) : null;
    for (let i = 0; i < stride; i++) {
      const a = i >= bytesPerPixel ? out[i - bytesPerPixel] : 0;
      const b = up ? up[i] : 0;
      const c = up && i >= bytesPerPixel ? up[i - bytesPerPixel] : 0;
      let predictor = 0;
      if (filter === 1) predictor = a;
      else if (filter === 2) predictor = b;
      else if (filter === 3) predictor = (a + b) >> 1;
      else if (filter === 4) {
        const p = a + b - c;
        const pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        predictor = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      out[i] = (line[i] + predictor) & 255;
    }
  }
  const sample = (y, x, channel) => {
    const index = x * channels + channel;
    if (depth === 16) return Math.round(rows.readUInt16BE(y * stride + index * 2) / 257);
    if (depth === 8) return rows[y * stride + index];
    const bit = index * depth;
    const value = (rows[y * stride + (bit >> 3)] >> (8 - depth - (bit & 7))) & ((1 << depth) - 1);
    return colour === 3 ? value : Math.round((value * 255) / ((1 << depth) - 1));
  };
  const rgb = Buffer.alloc(width * height * 3);
  const alpha = colour === 4 || colour === 6 ? Buffer.alloc(width * height) : null;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const p = y * width + x;
      if (colour === 2 || colour === 6) for (let k = 0; k < 3; k++) rgb[p * 3 + k] = sample(y, x, k);
      else if (colour === 3) {
        const index = sample(y, x, 0);
        if (index * 3 + 3 > palette.length) throw new Error(`palette index ${index} out of range`);
        palette.copy(rgb, p * 3, index * 3, index * 3 + 3);
      } else rgb.fill(sample(y, x, 0), p * 3, p * 3 + 3);
      if (alpha) alpha[p] = sample(y, x, channels - 1);
    }
  }
  return { width, height, rgb, alpha };
}

export function encodePng({ width, height, rgb }) {
  const raw = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; y++) rgb.copy(raw, y * (width * 3 + 1) + 1, y * width * 3, (y + 1) * width * 3);
  const chunk = (type, body) => {
    const head = Buffer.alloc(8);
    head.writeUInt32BE(body.length, 0);
    head.write(type, 4, 'latin1');
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(Buffer.concat([head.subarray(4), body])) >>> 0, 0);
    return Buffer.concat([head, body, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 2;
  return Buffer.concat([SIGNATURE, chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

export const NOISE = { level: 2, share: 0.005, mean: 0.5, max: 8 };

// Pixel comparison of two decoded images, alpha included when either has it; `heat` is a diff map over a dimmed `a`.
export function compareImages(a, b) {
  if (a.width !== b.width || a.height !== b.height) return { sizeMismatch: true, identical: false };
  const pixels = a.width * a.height;
  let max = 0, sum = 0, differing = 0, over = 0;
  const heat = Buffer.alloc(pixels * 3);
  const opacity = (img, p) => (img.alpha ? img.alpha[p] : 255);
  const channels = a.alpha || b.alpha ? 4 : 3;
  for (let p = 0; p < pixels; p++) {
    let d = Math.abs(opacity(a, p) - opacity(b, p));
    sum += channels === 4 ? d : 0;
    for (let k = 0; k < 3; k++) {
      const v = Math.abs(a.rgb[p * 3 + k] - b.rgb[p * 3 + k]);
      sum += v;
      if (v > d) d = v;
    }
    if (d > max) max = d;
    if (d) differing++;
    if (d > NOISE.level) over++;
    const o = p * 3;
    if (d) { heat[o] = Math.min(255, 96 + d * 4); heat[o + 1] = d > 32 ? Math.min(255, d * 2) : 0; }
    else heat.fill((a.rgb[o] + a.rgb[o + 1] + a.rgb[o + 2]) / 12, o, o + 3);
  }
  return { identical: max === 0, maxAbs: max, meanAbs: sum / (pixels * channels), differing: differing / pixels,
    overNoise: over / pixels, heat: { width: a.width, height: a.height, rgb: heat } };
}

export const classifyDiff = (d) => (d.identical ? 'identical' : !d.sizeMismatch && d.maxAbs <= NOISE.max &&
  d.overNoise < NOISE.share && d.meanAbs < NOISE.mean ? 'noise-level' : 'different');
