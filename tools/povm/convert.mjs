#!/usr/bin/env node
// Converts a Wavefront OBJ file, or a POV-Ray `mesh` or `mesh2` of literal numbers, to .povm (doc/povm.md).
import { closeSync, openSync, readSync } from 'node:fs';
import { basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { writePovm } from './povm.mjs';

const EPSILON = 1e-10;
const NONE = 0xffffffff;
const DECIMAL = /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/;

class Grow {
  constructor(Type) {
    this.a = new Type(1 << 12);
    this.n = 0;
  }
  push(...v) {
    if (this.n + v.length > this.a.length) {
      const b = new this.a.constructor(Math.max(2 * this.a.length, this.n + v.length));
      b.set(this.a.subarray(0, this.n));
      this.a = b;
    }
    for (const x of v) this.a[this.n++] = x;
  }
  get view() {
    return this.a.subarray(0, this.n);
  }
}

/** Welds values whose components are equal after rounding to `Type`, keeping the first one's index. */
class Welder {
  constructor(Type, width) {
    this.values = new Grow(Type);
    this.width = width;
    this.scratch = new Type(width);
    this.bits = new Uint32Array(this.scratch.buffer);
    this.slots = new Int32Array(1 << 12).fill(-1);
  }
  get count() {
    return this.values.n / this.width;
  }
  slot(v) {
    let h = 0x811c9dc5;
    for (const b of this.bits) h = Math.imul(h ^ b, 0x01000193) ^ (h >>> 15);
    const mask = this.slots.length - 1, a = this.values.a;
    for (let s = h & mask; ; s = (s + 1) & mask) {
      const i = this.slots[s];
      if (i < 0) return s;
      let k = 0;
      while (k < this.width && a[i * this.width + k] === v[k]) k++;
      if (k === this.width) return s;
    }
  }
  index(...v) {
    v.forEach((x, k) => (this.scratch[k] = x === 0 ? 0 : x));
    const w = Array.from(this.scratch);
    let s = this.slot(w);
    if (this.slots[s] >= 0) return this.slots[s];
    if (2 * (this.count + 1) > this.slots.length) {
      const old = this.slots;
      this.slots = new Int32Array(2 * old.length).fill(-1);
      for (const i of old) {
        if (i >= 0) {
          const u = Array.from(this.values.a.subarray(i * this.width, (i + 1) * this.width));
          u.forEach((x, k) => (this.scratch[k] = x));
          this.slots[this.slot(u)] = i;
        }
      }
      w.forEach((x, k) => (this.scratch[k] = x));
      s = this.slot(w);
    }
    this.slots[s] = this.count;
    this.values.push(...w);
    return this.count - 1;
  }
}

/** Synchronous byte reader over a file, refilled a megabyte at a time. */
class Scanner {
  constructor(path) {
    this.path = path;
    this.fd = openSync(path, 'r');
    this.buf = Buffer.allocUnsafe(1 << 20);
    this.pos = 0;
    this.end = 0;
    this.line = 1;
  }
  peek(ahead = 0) {
    if (this.pos + ahead >= this.end) {
      this.buf.copy(this.buf, 0, this.pos, this.end);
      this.end -= this.pos;
      this.pos = 0;
      let n;
      while (this.end <= ahead && (n = readSync(this.fd, this.buf, this.end, this.buf.length - this.end, null)) > 0) this.end += n;
      if (this.end <= ahead) return -1;
    }
    return this.buf[this.pos + ahead];
  }
  next() {
    const c = this.peek();
    if (c >= 0) this.pos++;
    if (c === 10) this.line++;
    return c;
  }
  error(message) {
    throw new Error(`${basename(this.path)}:${this.line}: ${message}`);
  }
  close() {
    closeSync(this.fd);
  }
}

const isDigit = (c) => c >= 48 && c <= 57;
const isWord = (c) => (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c === 95 || isDigit(c);
const isSpace = (c) => c === 32 || c === 9 || c === 13 || c === 10 || c === 12 || c === 11;

function warnOnce(warn, seen, key, message) {
  if (!seen.has(key)) {
    seen.add(key);
    warn(message);
  }
}

/** Reads an OBJ file's v, vt, vn and f statements; n-gons become fans, other statements are skipped with a warning. */
export function readObj(path, warn = console.warn) {
  const sc = new Scanner(path);
  const v = new Grow(Float32Array), vt = new Grow(Float32Array), vn = new Grow(Float64Array);
  const faces = new Grow(Uint32Array), fuv = new Grow(Uint32Array), fn = new Grow(Uint32Array);
  const seen = new Set();
  const words = [];
  try {
    for (;;) {
      words.length = 0;
      let c = sc.peek();
      if (c < 0) break;
      let word = '';
      while ((c = sc.next()) >= 0 && c !== 10) {
        if (c === 35) {
          while ((c = sc.next()) >= 0 && c !== 10);
          break;
        }
        if (isSpace(c)) {
          if (word) words.push(word);
          word = '';
        } else {
          word += String.fromCharCode(c);
        }
      }
      if (word) words.push(word);
      if (words.length === 0) continue;
      const line = sc.line - (c === 10 ? 1 : 0);
      const bad = (message) => { throw new Error(`${basename(path)}:${line}: ${message}`); };
      const floats = (n) => {
        if (words.length < n + 1) bad(`'${words[0]}' needs ${n} numbers`);
        return words.slice(1, n + 1).map((w) => {
          const x = Number(w);
          if (!DECIMAL.test(w) || !Number.isFinite(x)) bad(`'${w}' is not a number`);
          return x;
        });
      };
      switch (words[0]) {
        case 'v': v.push(...floats(3)); break;
        case 'vt': vt.push(...floats(2)); break;
        case 'vn': vn.push(...floats(3)); break;
        case 'f': {
          if (words.length < 4) bad('a face needs at least three vertices');
          const counts = [v.n / 3, vt.n / 2, vn.n / 3];
          const corners = words.slice(1).map((w) => {
            const parts = w.split('/');
            if (parts.length > 3 || parts[0] === '') bad(`bad face vertex '${w}'`);
            return [0, 1, 2].map((k) => {
              if (parts[k] === undefined || parts[k] === '') return NONE;
              const i = Number(parts[k]);
              if (!/^-?\d+$/.test(parts[k]) || i === 0) bad(`bad face index '${parts[k]}'`);
              const r = i < 0 ? counts[k] + i : i - 1;
              if (r < 0 || r >= counts[k]) bad(`face index ${i} is out of range`);
              return r;
            });
          });
          for (const k of [1, 2]) {
            const has = corners.filter((q) => q[k] !== NONE).length;
            if (has !== 0 && has !== corners.length) bad(`some but not all face vertices have ${k === 1 ? 'uv' : 'normal'} indices`);
          }
          for (let i = 1; i + 1 < corners.length; i++) {
            const tri = [corners[0], corners[i], corners[i + 1]];
            faces.push(...tri.map((q) => q[0]));
            fuv.push(...tri.map((q) => q[1]));
            fn.push(...tri.map((q) => q[2]));
          }
          break;
        }
        case 'g': case 'o': case 's': case 'usemtl': case 'mtllib':
          warnOnce(warn, seen, words[0], `${basename(path)}: '${words[0]}' statements are ignored; materials and groups are not converted`);
          break;
        default:
          warnOnce(warn, seen, words[0], `${basename(path)}: unsupported '${words[0]}' statements are ignored`);
      }
    }
  } finally {
    sc.close();
  }
  const mesh = { vertices: v.view, faces: faces.view };
  const column = (values, refs, width, name) => {
    const idx = refs.view;
    if (!idx.some((i) => i !== NONE)) return;
    for (let i = 0; i < idx.length; i++) if (idx[i] === NONE) idx[i] = 0;
    mesh[`${name}s`] = values.view;
    if (values.n / width !== mesh.vertices.length / 3 || idx.some((i, k) => i !== mesh.faces[k])) mesh[`${name}Indices`] = idx;
  };
  const n = vn.view;
  for (let i = 0; i < n.length; i += 3) n.set(normalized([n[i], n[i + 1], n[i + 2]], warn, seen, path), i);
  vn.a = Float32Array.from(n);
  column(vt, fuv, 2, 'uv');
  column(vn, fn, 3, 'normal');
  return mesh;
}

/** Tokens of an SDL file holding one mesh: numbers, words, directives and punctuation; anything else stops it. */
class SdlTokens {
  constructor(path) {
    this.sc = new Scanner(path);
    this.ahead = null;
  }
  error(message) {
    this.sc.error(message);
  }
  skipSpace() {
    const sc = this.sc;
    for (;;) {
      const c = sc.peek();
      if (isSpace(c)) sc.next();
      else if (c === 47 && sc.peek(1) === 47) while (sc.peek() >= 0 && sc.next() !== 10);
      else if (c === 47 && sc.peek(1) === 42) {
        sc.next(); sc.next();
        while (!(sc.peek() === 42 && sc.peek(1) === 47)) if (sc.next() < 0) this.error('unterminated comment');
        sc.next(); sc.next();
      } else return;
    }
  }
  read() {
    this.skipSpace();
    const sc = this.sc;
    const c = sc.peek();
    if (c < 0) return { t: 'eof' };
    const signed = (c === 45 || c === 43) && (isDigit(sc.peek(1)) || sc.peek(1) === 46);
    if (isDigit(c) || c === 46 || signed) {
      let s = String.fromCharCode(sc.next());
      for (let d = sc.peek(); isDigit(d) || d === 46 || d === 101 || d === 69 || ((d === 45 || d === 43) && /[eE]$/.test(s)); d = sc.peek()) {
        s += String.fromCharCode(sc.next());
      }
      const v = Number(s);
      if (!DECIMAL.test(s) || !Number.isFinite(v)) this.error(`'${s}' is not a number`);
      return { t: 'num', v, s };
    }
    if (isWord(c) || c === 35) {
      let s = String.fromCharCode(sc.next());
      while (isWord(sc.peek())) s += String.fromCharCode(sc.next());
      return { t: 'word', v: s };
    }
    if (c === 34) {
      let s = '';
      sc.next();
      for (let d = sc.next(); d !== 34; d = sc.next()) {
        if (d < 0 || d === 10) this.error('unterminated string');
        s += String.fromCharCode(d);
      }
      return { t: 'str', v: s };
    }
    return { t: 'punct', v: String.fromCharCode(sc.next()) };
  }
  peek() {
    return (this.ahead ??= this.read());
  }
  next() {
    const t = this.peek();
    this.ahead = null;
    return t;
  }
  is(v) {
    const t = this.peek();
    return t.t !== 'num' && t.v === v;
  }
  optional(v) {
    if (!this.is(v)) return false;
    this.next();
    return true;
  }
  unexpected(t, wanted) {
    const what = t.t === 'eof' ? 'end of file' : `'${t.t === 'num' ? t.s : t.v}'`;
    if (t.t === 'word' && !t.v.startsWith('#')) this.error(`expected ${wanted}, found ${what}: only literal numbers are supported, not identifiers or macros`);
    if ((t.t === 'punct' && '+-*/()?:!&|'.includes(t.v)) || (t.t === 'num' && /^[+-]/.test(t.s))) {
      this.error(`expected ${wanted}, found ${what}: only literal numbers are supported, not expressions`);
    }
    this.error(`expected ${wanted}, found ${what}`);
  }
  expect(v) {
    const t = this.next();
    if (t.t === 'num' || t.v !== v) this.unexpected(t, `'${v}'`);
  }
  number() {
    const t = this.next();
    if (t.t !== 'num') this.unexpected(t, 'a number');
    return t.v;
  }
  integer() {
    const t = this.next();
    if (t.t !== 'num' || !Number.isInteger(t.v) || t.v < 0) this.unexpected(t, 'a non-negative integer');
    return t.v;
  }
  vector(n, int = false) {
    this.expect('<');
    const v = [];
    for (let k = 0; k < n; k++) {
      if (k) this.expect(',');
      v.push(int ? this.integer() : this.number());
    }
    this.expect('>');
    return v;
  }
  skipBlock(warn, seen, name) {
    warnOnce(warn, seen, name, `${basename(this.sc.path)}: '${name}' is ignored; give textures in the scene`);
    this.expect('{');
    for (let depth = 1; depth > 0; ) {
      const t = this.next();
      if (t.t === 'eof') this.error(`unterminated '${name}'`);
      if (t.v === '{') depth++;
      if (t.v === '}') depth--;
    }
  }
}

function normalized([x, y, z], warn, seen, path) {
  if (Math.abs(x) < EPSILON && Math.abs(y) < EPSILON && Math.abs(z) < EPSILON) {
    warnOnce(warn, seen, 'zero', `${basename(path)}: a zero normal becomes <1,0,0>`);
    x = 1;
  }
  const l = Math.sqrt(x * x + y * y + z * z);
  return [x / l, y / l, z / l];
}

function differ(a, b) {
  const d = [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  return d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > EPSILON;
}

function degenerate(p1, p2, p3) {
  const a = [p1[0] - p2[0], p1[1] - p2[1], p1[2] - p2[2]], b = [p3[0] - p2[0], p3[1] - p2[1], p3[2] - p2[2]];
  const c = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  return Math.sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) === 0;
}

function readMesh1(tk, warn, seen, path) {
  const vertices = new Welder(Float32Array, 3), normals = new Welder(Float32Array, 3), uvs = new Welder(Float64Array, 2);
  const faces = new Grow(Uint32Array), fn = new Grow(Uint32Array), fuv = new Grow(Uint32Array);
  let smoothAny = false, uvAny = false;
  tk.expect('{');
  for (;;) {
    const t = tk.next();
    if (t.v === '}' && t.t === 'punct') break;
    if (t.v === 'inside_vector') {
      warnOnce(warn, seen, t.v, `${basename(path)}: 'inside_vector' is ignored; give it in the scene`);
      tk.vector(3);
      continue;
    }
    if (t.v !== 'triangle' && t.v !== 'smooth_triangle') tk.unexpected(t, "'triangle' or 'smooth_triangle' (modifiers belong in the scene)");
    const smoothSyntax = t.v === 'smooth_triangle';
    tk.expect('{');
    const p = [], n = [];
    for (let k = 0; k < 3; k++) {
      if (k) tk.optional(',');
      p.push(tk.vector(3));
      if (smoothSyntax) {
        tk.optional(',');
        n.push(normalized(tk.vector(3), warn, seen, path));
      }
    }
    let uv = [[0, 0], [0, 0], [0, 0]];
    if (tk.optional('uv_vectors')) {
      uv = [tk.vector(2), (tk.optional(','), tk.vector(2)), (tk.optional(','), tk.vector(2))];
      uvAny = true;
    }
    while (tk.is('texture')) {
      tk.next();
      tk.skipBlock(warn, seen, 'texture');
    }
    tk.expect('}');
    if (degenerate(...p)) continue;
    faces.push(...p.map((q) => vertices.index(...q)));
    fuv.push(...uv.map((q) => uvs.index(...q)));
    if (smoothSyntax && (differ(n[0], n[1]) || differ(n[0], n[2]))) {
      fn.push(...n.map((q) => normals.index(...q)));
      smoothAny = true;
    } else {
      fn.push(NONE, NONE, NONE);
    }
  }
  const mesh = { vertices: vertices.values.view, faces: faces.view };
  if (smoothAny) {
    mesh.normals = normals.values.view;
    mesh.normalIndices = fn.view.map((i) => (i === NONE ? 0 : i));
  }
  if (uvAny) {
    mesh.uvs = Float32Array.from(uvs.values.view);
    mesh.uvIndices = fuv.view;
  }
  return mesh;
}

function readVectors(tk, width, int = false) {
  tk.expect('{');
  const count = tk.integer();
  const out = new (int ? Uint32Array : Float64Array)(width * count);
  for (let i = 0; i < count; i++) {
    tk.optional(',');
    out.set(tk.vector(width, int), width * i);
  }
  tk.optional(',');
  return { count, out };
}

function readMesh2(tk, warn, seen, path) {
  const s = {};
  tk.expect('{');
  for (;;) {
    const t = tk.next();
    if (t.v === '}' && t.t === 'punct') break;
    switch (t.v) {
      case 'vertex_vectors': case 'normal_vectors': case 'uv_vectors': case 'normal_indices': case 'uv_indices': {
        if (s[t.v]) tk.error(`duplicate '${t.v}'`);
        s[t.v] = readVectors(tk, t.v === 'uv_vectors' ? 2 : 3, t.v.endsWith('indices'));
        tk.expect('}');
        break;
      }
      case 'face_indices': {
        if (s.face_indices) tk.error("duplicate 'face_indices'");
        tk.expect('{');
        const count = tk.integer();
        const out = new Uint32Array(3 * count);
        for (let i = 0; i < count; i++) {
          tk.optional(',');
          out.set(tk.vector(3, true), 3 * i);
          for (let k = 0; k < 3 && tk.optional(','); k++) {
            if (tk.peek().t !== 'num') break;
            tk.integer();
            warnOnce(warn, seen, 'texture index', `${basename(path)}: face texture indices are ignored; give textures in the scene`);
          }
        }
        tk.optional(',');
        tk.expect('}');
        s.face_indices = { count, out };
        break;
      }
      case 'texture_list':
        tk.skipBlock(warn, seen, 'texture_list');
        break;
      case 'inside_vector':
        warnOnce(warn, seen, t.v, `${basename(path)}: 'inside_vector' is ignored; give it in the scene`);
        tk.vector(3);
        break;
      default:
        tk.unexpected(t, 'a mesh2 section (modifiers belong in the scene)');
    }
  }
  if (!s.vertex_vectors || !s.face_indices) tk.error("mesh2 needs 'vertex_vectors' and 'face_indices'");
  const vertexCount = s.vertex_vectors.count, faces = s.face_indices.out;
  const mesh = { vertices: Float32Array.from(s.vertex_vectors.out), faces };
  if (s.normal_vectors && s.normal_vectors.count > 0) {
    const n = s.normal_vectors.out;
    mesh.normals = new Float32Array(n.length);
    for (let i = 0; i < n.length; i += 3) mesh.normals.set(normalized([n[i], n[i + 1], n[i + 2]], warn, seen, path), i);
    if (s.normal_indices) {
      if (s.normal_indices.count > faces.length / 3) tk.error('more normal indices than faces');
      mesh.normalIndices = new Uint32Array(faces.length);
      mesh.normalIndices.set(s.normal_indices.out);
    } else if (s.normal_vectors.count !== vertexCount) {
      tk.error('normal_indices are needed unless there is one normal per vertex');
    }
  }
  if (s.uv_vectors && s.uv_vectors.count > 0) {
    mesh.uvs = Float32Array.from(s.uv_vectors.out);
    if (s.uv_indices) {
      if (s.uv_indices.count !== faces.length / 3) tk.error('uv_indices must have one entry per face');
      mesh.uvIndices = s.uv_indices.out;
    } else if (s.uv_vectors.count === 1) {
      mesh.uvIndices = new Uint32Array(faces.length);
    } else if (s.uv_vectors.count !== vertexCount) {
      tk.error('uv_indices are needed unless there is one uv per vertex');
    }
  }
  return mesh;
}

/** Reads the one `mesh` or `mesh2` in an SDL file, optionally after `#version` and inside a `#declare` or `#local`. */
export function readPovMesh(path, warn = console.warn) {
  const tk = new SdlTokens(path);
  const seen = new Set();
  let mesh = null;
  try {
    for (let t = tk.next(); t.t !== 'eof'; t = tk.next()) {
      if (t.v === '#version') {
        tk.number();
        tk.expect(';');
      } else if ((t.v === '#declare' || t.v === '#local') && !mesh) {
        const name = tk.next();
        if (name.t !== 'word') tk.unexpected(name, 'a name');
        tk.expect('=');
        if (!tk.is('mesh') && !tk.is('mesh2')) tk.error(`only a mesh may be #declared here; #declare'd values are not supported`);
        continue;
      } else if ((t.v === 'mesh' || t.v === 'mesh2') && !mesh) {
        mesh = t.v === 'mesh' ? readMesh1(tk, warn, seen, path) : readMesh2(tk, warn, seen, path);
        tk.optional(';');
      } else {
        tk.unexpected(t, mesh ? 'end of file after the mesh' : "'mesh' or 'mesh2'");
      }
    }
  } finally {
    tk.sc.close();
  }
  if (!mesh) tk.error('no mesh found');
  return mesh;
}

async function main(args) {
  if (args.length !== 2) {
    console.error('usage: convert.mjs <input.obj | input.pov | input.inc> <output.povm>');
    process.exit(2);
  }
  const [input, output] = args;
  const mesh = /\.obj$/i.test(input) ? readObj(input) : readPovMesh(input);
  await writePovm(output, mesh);
  console.log(`${output}: ${mesh.vertices.length / 3} vertices, ${mesh.faces.length / 3} faces`);
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  main(process.argv.slice(2)).catch((e) => {
    console.error(e.message);
    process.exit(1);
  });
}
