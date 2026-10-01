import * as THREE from 'three';
import { examples, failures, notes, stress } from '../examples/index.ts';
import { intersections, orientationOf, parts, unionVolume, volume, buildMesh } from '../src/index.ts';
import type { Model } from '../src/index.ts';
import { modelGroup, stage, withGrid } from './scene.ts';
import { drawSlice } from './slice.ts';

const W = 260, H = 200;
const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
renderer.setPixelRatio(1);
renderer.setSize(W, H, false);
const { camera, show, render } = stage(renderer);
camera.aspect = W / H;
camera.updateProjectionMatrix();

interface Entry { name: string; section: string; model: Model; href: string; caption: string; grid?: [number, number] }

function label(name: string, model: Model) {
  const o = orientationOf(model), n = parts(model).length, open = o.some((x) => x.boundaryEdges > 0);
  const bits = [notes[name] ?? (n > 1 ? `composite (${n} parts)` : open ? 'open surface (later version)' : 'closed solid')];
  if (n > 1 && open && !notes[name]) bits.push('has open parts');
  if (!o.every((x) => x.orientable)) bits.push(open ? 'non-orientable (open)' : 'NON-ORIENTABLE: error');
  return { text: bits.join(' · '), error: !o.every((x) => x.orientable) && !open };
}

const entries: Entry[] = [];
for (const [name, model] of Object.entries(examples)) {
  const { text, error } = label(name, model);
  const section = error ? 'errors' : failures.includes(name) ? 'overlaps' : 'grid';
  entries.push({ name, section, model, href: `index.html?ex=${name}`, caption: text });
}
for (const [name, grid] of Object.entries(stress))
  entries.push({ name: `${name} @${grid.join('×')}`, section: 'stress', model: withGrid(examples[name], grid), grid, href: `index.html?ex=${name}&grid=${grid.join('x')}`, caption: 'stress test, high grid' });

function card(e: Entry) {
  const a = document.createElement('a');
  a.href = e.href;
  a.className = `card ${e.section}`;
  a.innerHTML = `<img width="${W}" height="${H}" alt="${e.name}"><span>${e.name}</span><small>${e.caption}</small><em></em>`;
  document.getElementById(e.section)!.append(a);
  return a;
}

const cards = entries.map((e) => [e, card(e)] as const);
let i = 0;
function next() {
  if (i === cards.length) {
    document.body.dataset.ready = 'all';
    return;
  }
  const [e, el] = cards[i++];
  show(modelGroup(e.model, { coarse: e.grid ? 1 : 2 }), 2.9);
  render();
  el.querySelector('img')!.src = renderer.domElement.toDataURL('image/png');
  if (e.section === 'overlaps') {
    const { self } = intersections(e.model), u = unionVolume(e.model).union;
    const div = parts(e.model).reduce((a, sh) => a + (volume(buildMesh(sh, undefined, undefined, false)) ?? 0), 0);
    el.querySelector('em')!.textContent = `${self} self-overlaps · union ${u.toFixed(3)} vs div. ${div.toFixed(3)}`;
    const c = document.createElement('canvas');
    el.append(c);
    drawSlice(c, e.model, 160);
  }
  if (e.section === 'errors') el.querySelector('em')!.textContent = 'no consistent inside: union undefined';
  setTimeout(next, 0);
}
next();
