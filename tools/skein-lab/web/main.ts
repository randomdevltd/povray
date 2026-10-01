import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { area, buildMesh, seamGap, selfIntersections, volume } from '../src/index.ts';
import type { Mesh, Shape } from '../src/index.ts';
import { examples } from '../examples/index.ts';
import type { ExampleName } from '../examples/index.ts';

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
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x15171c);
const camera = new THREE.PerspectiveCamera(40, 1, 0.01, 200);
const controls = new OrbitControls(camera, renderer.domElement);
scene.add(new THREE.HemisphereLight(0xdde4ff, 0x302820, 1.6));
const sun = new THREE.DirectionalLight(0xffffff, 2.2);
sun.position.set(3, 5, 4);
scene.add(sun);
const group = new THREE.Group();
scene.add(group);

const shaded = new THREE.MeshStandardMaterial({ color: 0xc8a27a, roughness: 0.65, side: THREE.DoubleSide });
const normalMat = new THREE.MeshNormalMaterial({ side: THREE.DoubleSide });
const lineMat = new THREE.LineBasicMaterial({ color: 0x101216, transparent: true, opacity: 0.45 });

const sources: Record<string, string> = await (await fetch('sources.json')).json();

function geometries(m: Mesh) {
  const surf = new THREE.BufferGeometry();
  surf.setAttribute('position', new THREE.BufferAttribute(new Float32Array(m.positions), 3));
  surf.setAttribute('normal', new THREE.BufferAttribute(m.normals, 3));
  surf.setIndex(new THREE.BufferAttribute(m.indices.slice(0, m.surfaceTris * 3), 1));
  const capIdx = m.indices.slice(m.surfaceTris * 3);
  const caps = new THREE.BufferGeometry();
  caps.setAttribute('position', new THREE.BufferAttribute(new Float32Array([...capIdx].flatMap((k) => [...m.positions.slice(3 * k, 3 * k + 3)])), 3));
  caps.computeVertexNormals();
  return [surf, caps];
}

function fmt(x: number | null, digits = 4) {
  return x === null ? 'open' : x.toFixed(digits);
}

function show(name: ExampleName) {
  const sh: Shape = examples[name];
  const t0 = performance.now();
  const m = buildMesh(sh);
  const t1 = performance.now();
  group.clear();
  for (const g of geometries(m)) {
    group.add(new THREE.Mesh(g, normals.checked ? normalMat : shaded));
    if (wire.checked) group.add(new THREE.LineSegments(new THREE.WireframeGeometry(g), lineMat));
  }
  const sphere = new THREE.Box3().setFromObject(group).getBoundingSphere(new THREE.Sphere());
  controls.target.copy(sphere.center);
  camera.position.copy(sphere.center).add(new THREE.Vector3(0.9, 0.7, 1.4).normalize().multiplyScalar(sphere.radius * 3.3));
  controls.update();
  $('source').textContent = sources[name] ?? '';
  $('m-grid').textContent = `${sh.grid[0]} × ${sh.grid[1]}`;
  $('m-area').textContent = fmt(area(m));
  $('m-volume').textContent = fmt(volume(m));
  $('m-seam').textContent = seamGap(sh, m).toExponential(2);
  $('m-self').textContent = '…';
  $('status').textContent = '';
  setTimeout(() => {
    const t2 = performance.now();
    $('m-self').textContent = String(selfIntersections(sh));
    $('m-time').textContent = `${(t1 - t0).toFixed(0)} + ${(performance.now() - t2).toFixed(0)} ms`;
    document.body.dataset.ready = name;
  }, 0);
}

for (const name of Object.keys(examples)) picker.add(new Option(name, name));
picker.value = params.get('ex') ?? 'torus';
const current = () => picker.value as ExampleName;
picker.onchange = wire.onchange = normals.onchange = () => show(current());

function resize() {
  const { clientWidth: w, clientHeight: h } = view;
  renderer.setSize(w, h, false);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}
addEventListener('resize', resize);
resize();
show(current());
renderer.setAnimationLoop(() => {
  controls.update();
  renderer.render(scene, camera);
});
