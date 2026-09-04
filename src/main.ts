// エントリ。M0 の雛形: Three.js が動くことを確かめるための回転する立方体 1 つ。
// 以降のマイルストーン（SPEC.md 8 章）で src/render・src/input・src/ui に置き換えていく。
import * as THREE from 'three';

const container = document.getElementById('app');
if (!container) throw new Error('#app が見つからない');

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
container.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0b0f1a);

const camera = new THREE.PerspectiveCamera(50, window.innerWidth / window.innerHeight, 0.1, 100);
camera.position.set(4, 3, 6);
camera.lookAt(0, 0, 0);

const light = new THREE.DirectionalLight(0xffffff, 2.0);
light.position.set(3, 5, 4);
scene.add(light, new THREE.AmbientLight(0x8090c0, 0.6));

const cube = new THREE.Mesh(
  new THREE.BoxGeometry(1.5, 1.5, 1.5),
  new THREE.MeshStandardMaterial({ color: 0x9fd3ff, roughness: 0.5, metalness: 0.1, emissive: 0x102040 }),
);
scene.add(cube);

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

const clock = new THREE.Clock();
renderer.setAnimationLoop(() => {
  const t = clock.getElapsedTime();
  cube.rotation.y = t * 0.6;
  cube.rotation.x = t * 0.25;
  renderer.render(scene, camera);
});
