// レンダラ・シーン・ライト・環境マップの初期化とアニメーションループ（SPEC.md 4 章）。
// ここはピースやパズルのことを知らない。何を置くかは main.ts が決める。

import * as THREE from 'three';
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js';

/** devicePixelRatio の上限（SPEC.md 4 章末）。 */
const MAX_PIXEL_RATIO = 2;

/**
 * すりガラス（transmission）用に three が裏で描くバッファの解像度倍率。
 *
 * three はこのバッファを「画面と同じ device pixel 数」で毎フレーム作り直す（＝画面をもう一度
 * 描くのに近いコスト）。屈折先はもともと roughness でぼけるので、半分に落としても見た目は
 * ほとんど変わらず、高 DPI 端末での負荷だけが目に見えて下がる。
 */
const TRANSMISSION_SCALE = 0.5;

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
  /** 直前のフレームのドローコール数（`?fps=1` の表示に使う）。 */
  drawCalls(): number;
  /** 生成した GPU 資源とイベントリスナを片付ける。 */
  dispose(): void;
};

/** container（既定は #app）いっぱいにレンダラを作り、ライトと環境マップを用意する。 */
export function createRenderContext(container: HTMLElement): RenderContext {
  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.transmissionResolutionScale = TRANSMISSION_SCALE;
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
    // 解像度の切り替えやモニタ間の移動で devicePixelRatio が変わることがあるので、
    // ここでも毎回クランプし直す（SPEC.md 4 章末の上限 2 が全経路で効くようにする）
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, MAX_PIXEL_RATIO));
    renderer.setSize(width, height, false);
  };
  resize();
  window.addEventListener('resize', resize);

  const clock = new THREE.Clock();
  // started = start() 済み、looping = 実際に requestAnimationFrame が回っている
  let started = false;
  let looping = false;
  let frameCallback: ((delta: number, elapsed: number) => void) | undefined;

  const stopLoop = (): void => {
    if (!looping) return;
    looping = false;
    renderer.setAnimationLoop(null);
  };

  const startLoop = (): void => {
    if (looping) return;
    looping = true;
    // 止まっていた間の時間が delta として一気に出ないよう、ここで捨てる
    clock.getDelta();
    renderer.setAnimationLoop((): void => {
      const delta = clock.getDelta();
      if (frameCallback) frameCallback(delta, clock.getElapsedTime());
      renderer.render(scene, camera);
    });
  };

  /**
   * タブが隠れている / 別ウィンドウの裏にある間はループを止める（SPEC.md 4 章の負荷対策）。
   * requestAnimationFrame は多くのブラウザで自動的に間引かれるが、止まらない環境
   * （バックグラウンドタブを回し続ける設定など）でも確実に止めるために自前で切る。
   */
  const onVisibilityChange = (): void => {
    if (!started) return;
    if (document.hidden) stopLoop();
    else startLoop();
  };
  document.addEventListener('visibilitychange', onVisibilityChange);

  return {
    renderer,
    scene,
    camera,
    start(onFrame): void {
      frameCallback = onFrame;
      started = true;
      if (!document.hidden) startLoop();
    },
    stop(): void {
      started = false;
      stopLoop();
    },
    resize,
    drawCalls(): number {
      return renderer.info.render.calls;
    },
    dispose(): void {
      started = false;
      stopLoop();
      frameCallback = undefined;
      window.removeEventListener('resize', resize);
      document.removeEventListener('visibilitychange', onVisibilityChange);
      scene.environment = null;
      environment.dispose();
      renderer.dispose();
      renderer.domElement.remove();
    },
  };
}
