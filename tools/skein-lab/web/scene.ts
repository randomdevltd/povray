import * as THREE from 'three';
import { buildMesh, parts } from '../src/index.ts';
import type { Mesh, Model } from '../src/index.ts';

export const shaded = new THREE.MeshStandardMaterial({ color: 0xc8a27a, roughness: 0.65, side: THREE.DoubleSide });
export const normalMat = new THREE.MeshNormalMaterial({ side: THREE.DoubleSide });
const lineMat = new THREE.LineBasicMaterial({ color: 0x101216, transparent: true, opacity: 0.45 });

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

export const withGrid = (model: Model, grid?: [number, number]): Model => (grid ? parts(model).map((sh) => ({ ...sh, grid })) : model);

export function modelGroup(model: Model, o: { wire?: boolean; normals?: boolean; coarse?: number } = {}) {
  const group = new THREE.Group();
  for (const sh of parts(model)) {
    const [nu, nv] = sh.grid.map((n) => Math.max(8, Math.round(n / (o.coarse ?? 1))));
    for (const g of geometries(buildMesh(sh, nu, nv))) {
      group.add(new THREE.Mesh(g, o.normals ? normalMat : shaded));
      if (o.wire) group.add(new THREE.LineSegments(new THREE.WireframeGeometry(g), lineMat));
    }
  }
  return group;
}

export function stage(renderer: THREE.WebGLRenderer) {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x15171c);
  scene.add(new THREE.HemisphereLight(0xdde4ff, 0x302820, 1.6));
  const sun = new THREE.DirectionalLight(0xffffff, 2.2);
  sun.position.set(3, 5, 4);
  scene.add(sun);
  const camera = new THREE.PerspectiveCamera(40, 1, 0.01, 200);
  const holder = new THREE.Group();
  scene.add(holder);
  const show = (group: THREE.Group, distance = 3.3) => {
    holder.clear();
    holder.add(group);
    const sphere = new THREE.Box3().setFromObject(group).getBoundingSphere(new THREE.Sphere());
    camera.position.copy(sphere.center).add(new THREE.Vector3(0.9, 0.7, 1.4).normalize().multiplyScalar(sphere.radius * distance));
    camera.lookAt(sphere.center);
    return sphere.center;
  };
  const render = () => renderer.render(scene, camera);
  return { scene, camera, show, render };
}
