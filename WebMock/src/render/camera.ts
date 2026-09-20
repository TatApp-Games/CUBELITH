// 注視点まわりの軌道カメラ（SPEC.md 3.3「カメラ」）。ドラッグで旋回、ホイール / ピンチでズーム。
// 依存を増やさないため OrbitControls は使わず自前で実装する。
// M3 でピース操作中にカメラを止められるよう、enabled で旋回 / ズームを切り替えられるようにしてある。

import * as THREE from 'three';

/** ドラッグ 1 px あたりの回転量（ラジアン）。 */
const ROTATE_SPEED = 0.006;
/** ホイール 1 ノッチあたりのズーム倍率の指数。 */
const ZOOM_SPEED = 0.0015;
/** 仰角の可動域。真上・真下で姿勢が壊れないよう少し内側で止める。 */
const MIN_POLAR = 0.08;
const MAX_POLAR = Math.PI - 0.08;
/** 目標値へ追従する割合（1 で即時）。 */
const DAMPING = 0.18;

export type OrbitCameraOptions = {
  /** 注視点。既定は原点。 */
  readonly target?: THREE.Vector3;
  /** 初期の距離。 */
  readonly radius?: number;
  readonly minRadius?: number;
  readonly maxRadius?: number;
};

export type OrbitCamera = {
  readonly camera: THREE.PerspectiveCamera;
  /** 旋回 / ズームの有効・無効。ピースを掴んでいる間は false にする。 */
  enabled: boolean;
  /**
   * 距離に倍率を掛ける（< 1 で寄る）。
   * ピースを掴んでいて enabled = false のときでも効く。ピース選択中の 2 本指ピンチ
   * （src/input/pieceInput）から呼ばれ、ズームだけは常に使えるようにするため。
   */
  zoomBy(scale: number): void;
  /** 半径 boundingRadius の球が画面に収まる距離へカメラを引く。 */
  frame(boundingRadius: number): void;
  /**
   * 自動旋回の速さ（rad/s）。0 で停止。
   * クリア演出（SPEC.md 5.2-4）で立方体の周囲をゆっくり回すのに使う。enabled とは独立で、
   * 手動操作を止めたまま（enabled = false）でも回り続ける。
   */
  setAutoRotate(speed: number): void;
  /**
   * 毎フレーム呼ぶ。目標値へ滑らかに追従してカメラ姿勢を更新する。
   * deltaSeconds は自動旋回の進み具合にだけ使う（フレームレートに依存させないため）。
   */
  update(deltaSeconds?: number): void;
  /** イベントリスナを外す。 */
  dispose(): void;
};

/** 2 点間の距離。ピンチ判定に使う。 */
function distance(a: { x: number; y: number }, b: { x: number; y: number }): number {
  return Math.hypot(a.x - b.x, a.y - b.y);
}

/**
 * domElement 上のポインタ操作でカメラを旋回・ズームさせる。
 * 角度は球面座標（azimuth / polar / radius）で持ち、update() で実際の位置に落とす。
 */
export function createOrbitCamera(
  camera: THREE.PerspectiveCamera,
  domElement: HTMLElement,
  options: OrbitCameraOptions = {},
): OrbitCamera {
  const target = (options.target ?? new THREE.Vector3()).clone();
  const minRadius = options.minRadius ?? 2;
  const maxRadius = options.maxRadius ?? 200;

  // 目標値（入力で動かす）と現在値（描画に使う）。差を DAMPING で詰めることで慣性が出る
  let goalAzimuth = Math.PI * 0.25;
  let goalPolar = Math.PI * 0.38;
  let goalRadius = THREE.MathUtils.clamp(options.radius ?? 20, minRadius, maxRadius);
  let azimuth = goalAzimuth;
  let polar = goalPolar;
  let radius = goalRadius;

  let enabled = true;
  // 自動旋回（クリア演出）。手動操作の enabled とは独立に効く
  let autoRotateSpeed = 0;
  // 追跡中のポインタ。1 本なら旋回、2 本ならピンチズーム
  const pointers = new Map<number, { x: number; y: number }>();
  let pinchDistance = 0;

  const applyRadius = (value: number): void => {
    goalRadius = THREE.MathUtils.clamp(value, minRadius, maxRadius);
  };

  const onPointerDown = (event: PointerEvent): void => {
    if (!enabled) return;
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    domElement.setPointerCapture(event.pointerId);
    if (pointers.size === 2) {
      const [a, b] = [...pointers.values()];
      if (a && b) pinchDistance = distance(a, b);
    }
  };

  const onPointerMove = (event: PointerEvent): void => {
    const previous = pointers.get(event.pointerId);
    if (!previous) return;
    const current = { x: event.clientX, y: event.clientY };
    pointers.set(event.pointerId, current);
    if (!enabled) return;

    if (pointers.size === 1) {
      // 1 本指 / マウスドラッグ: 旋回
      goalAzimuth -= (current.x - previous.x) * ROTATE_SPEED;
      goalPolar = THREE.MathUtils.clamp(
        goalPolar - (current.y - previous.y) * ROTATE_SPEED,
        MIN_POLAR,
        MAX_POLAR,
      );
      return;
    }
    if (pointers.size === 2) {
      // 2 本指: ピンチでズーム
      const [a, b] = [...pointers.values()];
      if (!a || !b) return;
      const next = distance(a, b);
      if (pinchDistance > 0 && next > 0) applyRadius(goalRadius * (pinchDistance / next));
      pinchDistance = next;
    }
  };

  const releasePointer = (event: PointerEvent): void => {
    pointers.delete(event.pointerId);
    if (domElement.hasPointerCapture(event.pointerId)) {
      domElement.releasePointerCapture(event.pointerId);
    }
    if (pointers.size < 2) pinchDistance = 0;
  };

  const onWheel = (event: WheelEvent): void => {
    if (!enabled) return;
    event.preventDefault();
    applyRadius(goalRadius * Math.exp(event.deltaY * ZOOM_SPEED));
  };

  const onContextMenu = (event: Event): void => {
    event.preventDefault();
  };

  domElement.addEventListener('pointerdown', onPointerDown);
  domElement.addEventListener('pointermove', onPointerMove);
  domElement.addEventListener('pointerup', releasePointer);
  domElement.addEventListener('pointercancel', releasePointer);
  domElement.addEventListener('wheel', onWheel, { passive: false });
  domElement.addEventListener('contextmenu', onContextMenu);

  const api: OrbitCamera = {
    camera,
    get enabled(): boolean {
      return enabled;
    },
    set enabled(value: boolean) {
      enabled = value;
      if (!value) {
        pointers.clear();
        pinchDistance = 0;
      }
    },
    zoomBy(scale: number): void {
      if (!Number.isFinite(scale) || scale <= 0) return;
      applyRadius(goalRadius * scale);
    },
    frame(boundingRadius: number): void {
      // 垂直画角に収まる距離。横長でない画面も考えて少し余裕を持たせる
      const halfFov = THREE.MathUtils.degToRad(camera.fov) / 2;
      const fit = boundingRadius / Math.sin(halfFov);
      applyRadius(fit * 1.15);
      radius = goalRadius;
    },
    setAutoRotate(speed: number): void {
      autoRotateSpeed = speed;
    },
    update(deltaSeconds = 1 / 60): void {
      if (autoRotateSpeed !== 0) {
        // タブ復帰などで delta が跳ねてもカメラが飛ばないよう上限を掛ける
        goalAzimuth += autoRotateSpeed * THREE.MathUtils.clamp(deltaSeconds, 0, 0.1);
      }
      azimuth += (goalAzimuth - azimuth) * DAMPING;
      polar += (goalPolar - polar) * DAMPING;
      radius += (goalRadius - radius) * DAMPING;
      const sinPolar = Math.sin(polar);
      camera.position.set(
        target.x + radius * sinPolar * Math.sin(azimuth),
        target.y + radius * Math.cos(polar),
        target.z + radius * sinPolar * Math.cos(azimuth),
      );
      camera.lookAt(target);
    },
    dispose(): void {
      domElement.removeEventListener('pointerdown', onPointerDown);
      domElement.removeEventListener('pointermove', onPointerMove);
      domElement.removeEventListener('pointerup', releasePointer);
      domElement.removeEventListener('pointercancel', releasePointer);
      domElement.removeEventListener('wheel', onWheel);
      domElement.removeEventListener('contextmenu', onContextMenu);
      pointers.clear();
    },
  };
  api.update();
  return api;
}
