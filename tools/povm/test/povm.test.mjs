// node --test tools/povm/test: the writer's contract and the converter's readers.
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { readPovm, writePovm } from '../povm.mjs';
import { readObj, readPovMesh } from '../convert.mjs';

const dir = mkdtempSync(join(tmpdir(), 'povm-'));
const file = (name, text) => {
  const path = join(dir, name);
  if (text !== undefined) writeFileSync(path, text);
  return path;
};
const quiet = () => {};

const square = () => ({
  vertices: new Float32Array([0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0]),
  faces: new Uint32Array([0, 1, 2, 0, 2, 3]),
});

test('writes the documented layout and reads it back', async () => {
  const mesh = {
    ...square(),
    normals: new Float32Array([0, 0, 1, 0, 0.6, 0.8]),
    normalIndices: new Uint32Array([0, 0, 1, 0, 1, 1]),
    uvs: new Float32Array([0, 0, 1, 0, 1, 1, 0, 1]),
  };
  const path = file('square.povm');
  await writePovm(path, mesh);
  const data = readFileSync(path);
  assert.equal(data.toString('latin1', 0, 4), 'POVM');
  assert.deepEqual([1, 2, 3, 4, 5, 6].map((i) => data.readUInt32LE(4 * i)), [1, 1, 4, 2, 4, 2]);
  assert.equal(data.length, 28 + 4 * (12 + 6 + 8 + 6 + 6));
  assert.deepEqual(await readPovm(path), mesh);
  assert.equal(existsSync(`${path}.tmp${process.pid}`), false);
});

test('rejects meshes a loader would refuse', async () => {
  const cases = [
    [{ ...square(), faces: new Uint32Array([0, 1, 4]) }, /faces\[2\] = 4 is out of range/],
    [{ ...square(), vertices: new Float32Array([0, 0, 0, 1, 0, 0, NaN, 1, 0, 0, 1, 0]) }, /vertex 2 is not finite/],
    [{ ...square(), normals: new Float32Array([0, 0, 1]) }, /1 normals for 4 vertices needs normalIndices/],
    [{ ...square(), uvs: new Float32Array(8), uvIndices: new Uint32Array(3) }, /uvIndices has 3 entries/],
    [{ ...square(), normalIndices: new Uint32Array(6) }, /normalIndices given without normals/],
    [{ ...square(), faces: [0, 1, 2] }, /faces must be a Uint32Array/],
  ];
  for (const [mesh, message] of cases) await assert.rejects(writePovm(file('bad.povm'), mesh), message);
  assert.equal(existsSync(file('bad.povm')), false);
});

test('OBJ: negative indices, fans, and ignored groups and materials', () => {
  const warnings = [];
  const body = 'v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\n';
  const positive = readObj(file('a.obj', `${body}g part\nusemtl red\nf 1//1 2//1 3//1 4//1\n`), (w) => warnings.push(w));
  const negative = readObj(file('b.obj', `${body}f -4//-1 -3//-1 -2//-1 -1//-1\n`), quiet);
  assert.deepEqual(Array.from(positive.faces), [0, 1, 2, 0, 2, 3]);
  assert.deepEqual(negative, positive);
  assert.deepEqual(Array.from(positive.normalIndices), [0, 0, 0, 0, 0, 0]);
  assert.equal(warnings.length, 2);
  assert.throws(() => readObj(file('c.obj', `${body}f 1 2 5\n`), quiet), /c\.obj:6: face index 5 is out of range/);
  assert.throws(() => readObj(file('d.obj', 'v 0x1 0 0\n'), quiet), /'0x1' is not a number/);
});

test('mesh {}: welds shared corners, drops degenerate triangles, keeps flat smooth_triangles flat', () => {
  const mesh = readPovMesh(file('m.inc', `#version 3.7;
    #declare M = mesh {
      triangle { <0,0,0>, <1,0,0>, <1,1,0> }
      triangle { <0,0,0>, <1,1,0>, <0,1,0> texture { T } }
      triangle { <0,0,0>, <1,1,0>, <2,2,0> }
      smooth_triangle { <0,0,0>, <0,0,2>, <1,1,0>, <0,0,1>, <0,1,0>, <0,1,0> }
      smooth_triangle { <0,0,0>, <0,0,1>, <0,1,0>, <0,0,1>, <-1,0,0>, <0,0,1> }
    }`), quiet);
  assert.deepEqual(Array.from(mesh.vertices), [0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, -1, 0, 0]);
  assert.deepEqual(Array.from(mesh.faces), [0, 1, 2, 0, 2, 3, 0, 2, 3, 0, 3, 4]);
  assert.deepEqual(Array.from(mesh.normals), [0, 0, 1, 0, 1, 0]);
  assert.deepEqual(Array.from(mesh.normalIndices), [0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0]);
});

test('mesh2: per-vertex normals and uvs need no indices, and texture indices are dropped', () => {
  const mesh = readPovMesh(file('m2.inc', `mesh2 {
    vertex_vectors { 3, <0,0,0>, <1,0,0>, <0,1,0> }
    normal_vectors { 3, <0,0,2>, <0,0,1>, <0,0,1> }
    uv_vectors { 3, <0,0>, <1,0>, <0,1> }
    texture_list { 1, texture { T } }
    face_indices { 1, <0,1,2>, 0 }
  }`), quiet);
  assert.deepEqual(Array.from(mesh.normals), [0, 0, 1, 0, 0, 1, 0, 0, 1]);
  assert.equal(mesh.normalIndices, undefined);
  assert.equal(mesh.uvIndices, undefined);
});

test('SDL: stops on anything but literal numbers', () => {
  const tri = (v) => `mesh { triangle { ${v}, <1,0,0>, <0,1,0> } }`;
  assert.throws(() => readPovMesh(file('e1.inc', tri('<1+2,0,0>')), quiet), /not expressions/);
  assert.throws(() => readPovMesh(file('e2.inc', tri('<A,0,0>')), quiet), /not identifiers or macros/);
  assert.throws(() => readPovMesh(file('e3.inc', tri('P')), quiet), /not identifiers or macros/);
  assert.throws(() => readPovMesh(file('e4.inc', `#declare S = 1;\n${tri('<0,0,0>')}`), quiet), /e4\.inc:1: only a mesh may be #declared/);
  assert.throws(() => readPovMesh(file('e5.inc', `#include "x.inc"\n${tri('<0,0,0>')}`), quiet), /expected 'mesh' or 'mesh2'/);
});
