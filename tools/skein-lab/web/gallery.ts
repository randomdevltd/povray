import * as THREE from 'three';
import { examples, failures } from '../examples/index.ts';
import { selfIntersections } from '../src/index.ts';
import { modelGroup, stage } from './scene.ts';

const W = 260, H = 200;
const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
renderer.setPixelRatio(1);
renderer.setSize(W, H, false);
const { camera, show, render } = stage(renderer);
camera.aspect = W / H;
camera.updateProjectionMatrix();

const names = Object.keys(examples);
const ordered = [...names.filter((n) => !failures.includes(n)), ...failures];

function card(name: string) {
  const a = document.createElement('a');
  a.href = `index.html?ex=${name}`;
  a.className = failures.includes(name) ? 'card fail' : 'card';
  a.innerHTML = `<img width="${W}" height="${H}" alt="${name}"><span>${name}</span><em></em>`;
  document.getElementById(failures.includes(name) ? 'failures' : 'grid')!.append(a);
  return a;
}

const cards = ordered.map((name) => [name, card(name)] as const);
let i = 0;
function next() {
  if (i === cards.length) {
    document.body.dataset.ready = 'all';
    return;
  }
  const [name, el] = cards[i++];
  show(modelGroup(examples[name], { coarse: 2 }), 2.9);
  render();
  el.querySelector('img')!.src = renderer.domElement.toDataURL('image/png');
  if (failures.includes(name)) el.querySelector('em')!.textContent = `${selfIntersections(examples[name])} self-x`;
  setTimeout(next, 0);
}
next();
