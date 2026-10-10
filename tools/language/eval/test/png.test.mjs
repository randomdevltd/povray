// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import test from 'node:test';
import { crc32, deflateSync } from 'node:zlib';
import { classifyDiff, compareImages, decodePng, encodePng } from '../png.mjs';

const image = (width, height, f) => {
  const rgb = Buffer.alloc(width * height * 3);
  for (let i = 0; i < rgb.length; i++) rgb[i] = f(i);
  return { width, height, rgb, alpha: null };
};
const chunk = (type, body) => {
  const head = Buffer.from(`\0\0\0\0${type}`, 'latin1');
  head.writeUInt32BE(body.length, 0);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([head.subarray(4), body])) >>> 0, 0);
  return Buffer.concat([head, body, crc]);
};
const png = (width, height, depth, colour, raw, extra = []) => {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = depth;
  ihdr[9] = colour;
  return Buffer.concat([encodePng(image(1, 1, () => 0)).subarray(0, 8), chunk('IHDR', ihdr), ...extra,
    chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
};

test('round-trips RGB8', () => {
  const a = image(7, 5, (i) => (i * 37) & 255);
  assert.deepEqual(decodePng(encodePng(a)), a);
});

test('undoes Paeth filtering, rounds 16-bit samples and keeps alpha', () => {
  const row = Buffer.from([4, ...Array.from({ length: 16 }, (_, i) => i * 10)]);
  const decoded = decodePng(png(2, 2, 16, 6, Buffer.concat([row, row])));
  assert.deepEqual([...decoded.rgb.subarray(0, 6)], [0, 20, 40, 80, 120, 160]);
  assert.equal(decoded.alpha[0], 60);
});

test('rejects malformed files loudly', () => {
  assert.throws(() => decodePng(png(1, 1, 8, 2, Buffer.from([9, 1, 2, 3]))), /filter type 9/);
  assert.throws(() => decodePng(png(0, 1, 8, 2, Buffer.from([0]))), /no pixels/);
  assert.throws(() => decodePng(png(2, 2, 8, 2, Buffer.from([0, 1, 2, 3]))), /shorter/);
  assert.throws(() => decodePng(png(1, 1, 8, 3, Buffer.from([0, 5]), [chunk('PLTE', Buffer.from([1, 2, 3]))])), /out of range/);
  const corrupt = png(1, 1, 8, 2, Buffer.from([0, 1, 2, 3]));
  corrupt[corrupt.length - 20] ^= 0xff;
  assert.throws(() => decodePng(corrupt), /CRC/);
});

test('classifies identical, noise-level and different, alpha included', () => {
  const a = image(100, 100, () => 100);
  assert.equal(classifyDiff(compareImages(a, a)), 'identical');
  assert.equal(classifyDiff(compareImages(a, image(100, 100, (i) => (i % 97 ? 100 : 102)))), 'noise-level');
  assert.equal(classifyDiff(compareImages(a, image(100, 100, (i) => (i < 90 ? 200 : 100)))), 'different');
  assert.equal(classifyDiff(compareImages(a, image(10, 10, () => 100))), 'different');
  assert.equal(classifyDiff(compareImages(a, { ...a, alpha: Buffer.alloc(10000, 0) })), 'different');
});
