import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { measure, normalAt, parts } from '../src/index.ts';
import { examples } from '../examples/index.ts';
import { modelGroup, stage, withGrid } from './scene.ts';
import { drawSlice } from './slice.ts';

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const params = new URLSearchParams(location.search);
const view = $<HTMLDivElement>('view');
const picker = $<HTMLSelectElement>('example');
const wire = $<HTMLInputElement>('wire');
const normals = $<HTMLInputElement>('normals');
const sliceBox = $<HTMLInputElement>('slice');
sliceBox.checked = params.get('slice') === '1';
const gridParam = params.get('grid')?.split('x').map(Number) as [number, number] | undefined;
wire.checked = params.get('wire') === '1';
normals.checked = params.get('normals') === '1';

const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
renderer.setPixelRatio(devicePixelRatio);
view.prepend(renderer.domElement);
const { camera, show: place, render } = stage(renderer);
const controls = new OrbitControls(camera, renderer.domElement);
const sources: Record<string, string> = await (await fetch('sources.json')).json();

function show(name: string) {
  const model = withGrid(examples[name], gridParam);
  const t0 = performance.now();
  controls.target.copy(place(modelGroup(model, { wire: wire.checked, normals: normals.checked })));
  const focus = params.get('focus')?.split(',').map(Number);
  if (focus) {
    const sh = parts(model)[0], p = sh.surface(focus[0], focus[1]), n = normalAt(sh.surface, focus[0], focus[1]), d = focus[2] ?? 0.6;
    controls.target.set(...p);
    camera.position.set(p[0] + d * (n[0] + 0.3), p[1] + d * (n[1] + 0.4), p[2] + d * n[2]);
  }
  controls.update();
  const t1 = performance.now();
  $('source').textContent = sources[name] ?? '';
  $('m-grid').textContent = parts(model).map((sh) => `${sh.grid[0]}×${sh.grid[1]}`).slice(0, 2).join(', ') + (parts(model).length > 2 ? ` +${parts(model).length - 2}` : '');
  for (const id of ['m-area', 'm-volume', 'm-union', 'm-seam', 'm-self', 'm-contacts', 'm-orient']) $(id).textContent = '…';
  setTimeout(() => {
    const t2 = performance.now(), m = measure(model);
    $('m-area').textContent = m.area.toFixed(4);
    $('m-volume').textContent = m.volume === null ? 'open' : m.volume.toFixed(4);
    $('m-seam').textContent = m.seamGap.toExponential(2);
    $('m-union').textContent = m.union === null ? '—' : m.union.toFixed(4);
    $('m-self').textContent = String(m.selfIntersections);
    $('m-self').classList.toggle('warn', m.selfIntersections > 0);
    $('m-contacts').textContent = String(m.contacts);
    $('m-orient').textContent = m.orientable ? 'yes' : m.open ? 'no (open)' : 'NO: error';
    $('m-orient').classList.toggle('bad', !m.orientable && !m.open);
    const canvas = $<HTMLCanvasElement>('slicecanvas');
    canvas.hidden = !sliceBox.checked;
    if (sliceBox.checked && m.orientable && !m.open) {
      const s = drawSlice(canvas, model);
      $('slicenote').textContent = `winding at z = ${s.z.toFixed(2)}: ${s.min}..${s.max}`;
    } else $('slicenote').textContent = '';
    $('m-time').textContent = `${(t1 - t0).toFixed(0)} + ${(performance.now() - t2).toFixed(0)} ms`;
    document.body.dataset.ready = name;
  }, 0);
}

for (const name of Object.keys(examples)) picker.add(new Option(name, name));
picker.value = params.get('ex') ?? 'torus';
picker.onchange = wire.onchange = normals.onchange = sliceBox.onchange = () => {
  history.replaceState(null, '', `?ex=${picker.value}${wire.checked ? '&wire=1' : ''}${normals.checked ? '&normals=1' : ''}${sliceBox.checked ? '&slice=1' : ''}${gridParam ? `&grid=${gridParam.join('x')}` : ''}`);
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
