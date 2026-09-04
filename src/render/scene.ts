// レンダラ・シーン・ライト・環境マップの初期化とアニメーションループ（SPEC.md 4 章）。
// ここはピースやパズルのことを知らない。何を置くかは main.ts が決める。

import * as THREE from 'three';
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js';

/** devicePixelRatio の上限（SPEC.md 4 章末）。 */
const MAX_PIXEL_RATIO = 2;

/** シーン一式。start でフレーム更新のコールバックを登録する。 */
export type RenderContext = {
  readonly renderer: THREE.WebGLRenderer;
  readonly scene: THREE.Scene;
  readonly camera: THREE.PerspectiveCamera;
  /** アニメーションループを開始する。onFrame は毎フレーム、描画の直前に呼ばれる。 */
  start(onFrame?: (delta: number, elapsed: number) => void): void;
  /** ループを止める。 */
  stop(): void;
  /** 表示領域のサイズをコンテナに合わせ直す。resize イベントからも自動で呼ばれる。 */
  resize(): void;
  /** 生成した GPU 資源とイベントリスナを片付ける。 */
  dispose(): void;
};

/** container（既定は #app）いっぱいにレンダラを作り、ライトと環境マップを用意する。 */
export function createRenderContext(container: HTMLElement): RenderContext {
  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, MAX_PIXEL_RATIO));
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.15;
  container.appendChild(renderer.domElement);

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x0b0f1a);

  const camera = new THREE.PerspectiveCamera(50, 1, 0.1, 500);
  camera.position.set(0, 0, 10);

  // 動的ライトはディレクショナル 1 灯だけ。影は使わない（SPEC.md 4 章）
  const keyLight = new THREE.DirectionalLight(0xffffff, 2.4);
  keyLight.position.set(6, 9, 7);
  scene.add(keyLight);

  // 環境マップは RoomEnvironment を PMREM に焼いて使う。すりガラスの映り込みはこれが担う
  const pmrem = new THREE.PMREMGenerator(renderer);
  const roomScene = new RoomEnvironment();
  const environment = pmrem.fromScene(roomScene, 0.04).texture;
  scene.environment = environment;
  roomScene.dispose();
  pmrem.dispose();

  const resize = (): void => {
    const width = container.clientWidth || window.innerWidth;
    const height = container.clientHeight || window.innerHeight;
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    renderer.setSize(width, height, false);
  };
  resize();
  window.addEventListener('resize', resize);

  const clock = new THREE.Clock();
  let running = false;

  return {
    renderer,
    scene,
    camera,
    start(onFrame): void {
      running = true;
      renderer.setAnimationLoop((): void => {
        if (!running) return;
        const delta = clock.getDelta();
        if (onFrame) onFrame(delta, clock.getElapsedTime());
        renderer.render(scene, camera);
      });
    },
    stop(): void {
      running = false;
      renderer.setAnimationLoop(null);
    },
    resize,
    dispose(): void {
      running = false;
      renderer.setAnimationLoop(null);
      window.removeEventListener('resize', resize);
      scene.environment = null;
      environment.dispose();
      renderer.dispose();
      renderer.domElement.remove();
    },
  };
}
