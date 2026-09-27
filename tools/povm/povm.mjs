// Reads and writes .povm mesh files, as specified in doc/povm.md. Node 24, no dependencies.
import { createWriteStream } from 'node:fs';
import { readFile, rename, rm } from 'node:fs/promises';
import { once } from 'node:events';
import { endianness } from 'node:os';

export const POVM_VERSION = 1;
const MAGIC = 'POVM';
const NORMAL_INDICES = 1;
const UV_INDICES = 2;
const HEADER_BYTES = 28;
const MAX_VERTICES = 2 ** 30;
const MAX_INDEX = 2 ** 31 - 1;
const CHUNK_BYTES = 1 << 22;

function fail(message) {
  throw new Error(`povm: ${message}`);
}

function typed(value, Type, name, width, optional) {
  if (value === undefined || value === null) {
    if (!optional) fail(`${name} is required`);
    return null;
  }
  if (!(value instanceof Type)) fail(`${name} must be a ${Type.name}`);
  if (value.length % width !== 0) fail(`${name} length ${value.length} is not a multiple of ${width}`);
  return value;
}

function checkIndices(indices, count, name) {
  for (let i = 0; i < indices.length; i++) {
    if (indices[i] >= count) fail(`${name}[${i}] = ${indices[i]} is out of range (${count} entries)`);
  }
}

function checkColumn(values, indices, width, faces, vertexCount, name) {
  const count = values ? values.length / width : 0;
  if (indices) {
    if (!values || count === 0) fail(`${name}Indices given without ${name}s`);
    if (indices.length !== faces.length) fail(`${name}Indices has ${indices.length} entries; faces has ${faces.length}`);
    if (count > MAX_INDEX) fail(`too many ${name}s`);
    checkIndices(indices, count, `${name}Indices`);
  } else if (count !== 0 && count !== vertexCount) {
    fail(`${count} ${name}s for ${vertexCount} vertices needs ${name}Indices`);
  }
  return count;
}

/** Validates a mesh as writePovm takes it and returns its header counts. */
export function checkMesh(mesh) {
  const vertices = typed(mesh.vertices, Float32Array, 'vertices', 3, false);
  const faces = typed(mesh.faces, Uint32Array, 'faces', 3, false);
  const normals = typed(mesh.normals, Float32Array, 'normals', 3, true);
  const uvs = typed(mesh.uvs, Float32Array, 'uvs', 2, true);
  const normalIndices = typed(mesh.normalIndices, Uint32Array, 'normalIndices', 3, true);
  const uvIndices = typed(mesh.uvIndices, Uint32Array, 'uvIndices', 3, true);
  const vertexCount = vertices.length / 3;
  if (vertexCount === 0) fail('no vertices');
  if (vertexCount >= MAX_VERTICES) fail(`${vertexCount} vertices; at most ${MAX_VERTICES - 1}`);
  if (faces.length === 0) fail('no faces');
  if (faces.length / 3 > MAX_INDEX / 3) fail('too many faces');
  for (let i = 0; i < vertices.length; i++) {
    if (!Number.isFinite(vertices[i])) fail(`vertex ${Math.floor(i / 3)} is not finite`);
  }
  checkIndices(faces, vertexCount, 'faces');
  const normalCount = checkColumn(normals, normalIndices, 3, faces, vertexCount, 'normal');
  const uvCount = checkColumn(uvs, uvIndices, 2, faces, vertexCount, 'uv');
  return {
    flags: (normalIndices ? NORMAL_INDICES : 0) | (uvIndices ? UV_INDICES : 0),
    counts: [vertexCount, normalCount, uvCount, faces.length / 3],
    sections: [vertices, normalCount ? normals : null, uvCount ? uvs : null, faces, normalIndices, uvIndices].filter(Boolean),
  };
}

// Streams `mesh` ({ vertices, faces, normals?, uvs?, normalIndices?, uvIndices? }) to `path` as .povm, through a
// temporary file renamed into place so a reader never sees half a file.
export async function writePovm(path, mesh) {
  if (endianness() !== 'LE') fail('writing needs a little-endian host');
  const { flags, counts, sections } = checkMesh(mesh);
  const header = Buffer.alloc(HEADER_BYTES);
  header.write(MAGIC, 0, 'latin1');
  [POVM_VERSION, flags, ...counts].forEach((v, i) => header.writeUInt32LE(v, 4 + 4 * i));

  const temp = `${path}.tmp${process.pid}`;
  const out = createWriteStream(temp);
  const failed = once(out, 'error').then(([e]) => { throw e; });
  failed.catch(() => {});
  const write = async (chunk) => {
    if (!out.write(chunk)) await Promise.race([once(out, 'drain'), failed]);
  };
  try {
    await write(header);
    for (const a of sections) {
      for (let off = 0; off < a.byteLength; off += CHUNK_BYTES) {
        await write(Buffer.from(a.buffer, a.byteOffset + off, Math.min(CHUNK_BYTES, a.byteLength - off)));
      }
    }
    out.end();
    await Promise.race([once(out, 'close'), failed]);
    await rename(temp, path);
  } catch (e) {
    out.destroy();
    await rm(temp, { force: true });
    throw e;
  }
}

/** Reads a whole .povm file into the object writePovm takes; a helper for tests and tools that checks only its size. */
export async function readPovm(path) {
  const data = await readFile(path);
  if (data.length < HEADER_BYTES || data.toString('latin1', 0, 4) !== MAGIC) fail(`${path} is not a povm file`);
  const [version, flags, vertices, normals, uvs, faces] = [0, 1, 2, 3, 4, 5].map((i) => data.readUInt32LE(4 + 4 * i));
  if (version !== POVM_VERSION) fail(`${path} has version ${version}`);
  const sizes = [
    ['vertices', Float32Array, 3 * vertices],
    ['normals', Float32Array, 3 * normals],
    ['uvs', Float32Array, 2 * uvs],
    ['faces', Uint32Array, 3 * faces],
    ['normalIndices', Uint32Array, flags & NORMAL_INDICES ? 3 * faces : 0],
    ['uvIndices', Uint32Array, flags & UV_INDICES ? 3 * faces : 0],
  ];
  const expected = sizes.reduce((n, [, , words]) => n + 4 * words, HEADER_BYTES);
  if (data.length !== expected) fail(`${path} has ${data.length} bytes where its header needs ${expected}`);
  const mesh = {};
  let off = HEADER_BYTES;
  for (const [name, Type, words] of sizes) {
    if (words) mesh[name] = new Type(data.buffer.slice(data.byteOffset + off, data.byteOffset + off + 4 * words));
    off += 4 * words;
  }
  return mesh;
}
