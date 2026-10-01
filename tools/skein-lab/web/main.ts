import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { measure, parts } from '../src/index.ts';
import { examples } from '../examples/index.ts';
import { modelGroup, stage } from './scene.ts';

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const params = new URLSearchParams(location.search);
const view = $<HTMLDivElement>('view');
const picker = $<HTMLSelectElement>('example');
const wire = $<HTMLInputElement>('wire');
const normals = $<HTMLInputElement>('normals');
wire.checked = params.get('wire') === '1';
normals.checked = params.get('normals') === '1';

const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
renderer.setPixelRatio(devicePixelRatio);
view.prepend(renderer.domElement);
const { camera, show: place, render } = stage(renderer);
const controls = new OrbitControls(camera, renderer.domElement);
const sources: Record<string, string> = await (await fetch('sources.json')).json();

function show(name: string) {
  const model = examples[name];
  const t0 = performance.now();
  controls.target.copy(place(modelGroup(model, { wire: wire.checked, normals: normals.checked })));
  controls.update();
  const t1 = performance.now();
  $('source').textContent = sources[name] ?? '';
  $('m-grid').textContent = parts(model).map((sh) => `${sh.grid[0]}×${sh.grid[1]}`).slice(0, 2).join(', ') + (parts(model).length > 2 ? ` +${parts(model).length - 2}` : '');
  for (const id of ['m-area', 'm-volume', 'm-seam', 'm-self']) $(id).textContent = '…';
  setTimeout(() => {
    const t2 = performance.now(), m = measure(model);
    $('m-area').textContent = m.area.toFixed(4);
    $('m-volume').textContent = m.volume === null ? 'open' : m.volume.toFixed(4);
    $('m-seam').textContent = m.seamGap.toExponential(2);
    $('m-self').textContent = String(m.selfIntersections);
    $('m-self').classList.toggle('bad', m.selfIntersections > 0);
    $('m-time').textContent = `${(t1 - t0).toFixed(0)} + ${(performance.now() - t2).toFixed(0)} ms`;
    document.body.dataset.ready = name;
  }, 0);
}

for (const name of Object.keys(examples)) picker.add(new Option(name, name));
picker.value = params.get('ex') ?? 'torus';
picker.onchange = wire.onchange = normals.onchange = () => {
  history.replaceState(null, '', `?ex=${picker.value}${wire.checked ? '&wire=1' : ''}${normals.checked ? '&normals=1' : ''}`);
  show(picker.value);
};

function resize() {
  const { clientWidth: w, clientHeight: h } = view;
  renderer.setSize(w, h, false);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}
addEventListener('resize', resize);
resize();
show(picker.value);
renderer.setAnimationLoop(() => {
  controls.update();
  render();
});
