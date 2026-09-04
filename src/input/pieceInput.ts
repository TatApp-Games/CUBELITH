// ポインタ / タッチ入力をピース操作に変換する（SPEC.md 3.3）。
// クリック / タップで選択、ドラッグでボクセル単位の移動、ホイール / 2 本指上下で奥行き移動。
// 選択中はカメラの旋回・ズームを止め、非選択時のドラッグはカメラに任せる（排他）。

import * as THREE from 'three';
import { addVec3, type Vec3 } from '../core/grid';
import type { OrbitCamera } from '../render/camera';
import { axisStepVector, dragAxes, type DragAxes } from './axisMapping';

/**
 * 1 マス動かすのに必要なドラッグ量（px）の下限・上限。
 * 基準はピースの奥行きでの「1 ボクセルの画面上の大きさ」なので、画面サイズやカメラ距離が
 * 変わっても「ボクセル 1 個分ドラッグすれば 1 マス動く」体感になる。極端なズームで感度が
 * 壊れないよう両端をクランプする。
 */
const MIN_PIXELS_PER_VOXEL = 14;
const MAX_PIXELS_PER_VOXEL = 160;

/** ホイールの累積量がこれを超えるたびに奥行きを 1 マス動かす。 */
const WHEEL_PIXELS_PER_STEP = 60;
/** 2 本指の上下スワイプがこれだけ進むたびに奥行きを 1 マス動かす。 */
const TWO_FINGER_PIXELS_PER_STEP = 48;

export type PieceInputOptions = {
  /** イベントを受けるキャンバス。 */
  readonly domElement: HTMLElement;
  readonly camera: THREE.PerspectiveCamera;
  /** ピースの InstancedMesh を子に持つルート（レイキャストの対象）。 */
  readonly root: THREE.Object3D;
  /** 選択中はカメラを止めるために触る。 */
  readonly orbit: OrbitCamera;
  /** ピースの現在のグリッド座標。ドラッグ感度の基準にする。 */
  readonly gridPositionOf: (pieceId: number) => Vec3 | undefined;
  /** 選択が変わったとき（解除は null）。 */
  readonly onSelectionChange: (pieceId: number | null) => void;
  /** ピースをグリッド上で delta マス動かす要求。 */
  readonly onMove: (pieceId: number, delta: Vec3) => void;
  /**
   * 操作していたピースから手を離したとき（pointerup / pointercancel）。
   * マグネットスナップ（SPEC.md 3.5）を掛けるきっかけに使う。
   */
  readonly onRelease?: (pieceId: number) => void;
};

export type PieceInput = {
  /** 現在アクティブなピース id。無ければ null。 */
  selectedPieceId(): number | null;
  /** 外（UI など）から選択を変える。 */
  select(pieceId: number | null): void;
  /** アクティブなピースを奥行き方向に 1 マス動かす（+1 = カメラから遠ざかる）。 */
  moveDepth(dir: 1 | -1): void;
  /** イベントリスナを外す。 */
  dispose(): void;
};

/** ドラッグ 1 本分の状態。開始時に軸と感度を固定し、途中でカメラが動いてもぶれないようにする。 */
type DragState = {
  readonly pointerId: number;
  readonly pieceId: number;
  readonly startX: number;
  readonly startY: number;
  readonly axes: DragAxes;
  readonly pixelsPerVoxel: number;
  /** これまでに反映済みのマス数。差分だけを onMove に流す。 */
  appliedRight: number;
  appliedUp: number;
};

/** 2 本指の上下スワイプの状態。 */
type TwoFingerState = { lastY: number; accumulated: number };

/** ホイールの delta を px 相当に正規化する（行 / ページ単位のブラウザ対策）。 */
function wheelPixels(event: WheelEvent): number {
  if (event.deltaMode === 1) return event.deltaY * 16;
  if (event.deltaMode === 2) return event.deltaY * 100;
  return event.deltaY;
}

export function createPieceInput(options: PieceInputOptions): PieceInput {
  const { domElement, camera, root, orbit } = options;

  const raycaster = new THREE.Raycaster();
  const ndc = new THREE.Vector2();
  const scratch = new THREE.Vector3();

  let selected: number | null = null;
  let drag: DragState | null = null;
  let twoFinger: TwoFingerState | null = null;
  let wheelAccumulated = 0;

  /** 追跡中のポインタ（主ボタン / 指のみ）。 */
  const pointers = new Map<number, { x: number; y: number }>();

  const averageY = (): number => {
    if (pointers.size === 0) return 0;
    let sum = 0;
    for (const p of pointers.values()) sum += p.y;
    return sum / pointers.size;
  };

  /**
   * 現在のカメラからドラッグ軸を作る。
   * 解釈: ピースのルートは平行移動しかしていないので、ワールド軸とグリッド軸は一致する。
   */
  const currentAxes = (): DragAxes => {
    camera.updateMatrixWorld();
    const right = new THREE.Vector3().setFromMatrixColumn(camera.matrixWorld, 0);
    const up = new THREE.Vector3().setFromMatrixColumn(camera.matrixWorld, 1);
    // カメラは自身の -Z を向く
    const forward = new THREE.Vector3().setFromMatrixColumn(camera.matrixWorld, 2).negate();
    return dragAxes(right, up, forward);
  };

  /** ピースの位置での「1 ボクセル = 何 px」。ドラッグ量の閾値に使う。 */
  const pixelsPerVoxelAt = (pieceId: number): number => {
    const rect = domElement.getBoundingClientRect();
    const height = rect.height > 0 ? rect.height : 1;
    const grid = options.gridPositionOf(pieceId);
    root.updateMatrixWorld();
    scratch.set(grid?.x ?? 0, grid?.y ?? 0, grid?.z ?? 0);
    root.localToWorld(scratch);
    const distance = Math.max(camera.position.distanceTo(scratch), 1e-3);
    const halfFov = THREE.MathUtils.degToRad(camera.fov) / 2;
    const pixels = height / (2 * distance * Math.tan(halfFov));
    return THREE.MathUtils.clamp(pixels, MIN_PIXELS_PER_VOXEL, MAX_PIXELS_PER_VOXEL);
  };

  /** 画面座標のピースを拾う。何も無ければ null。 */
  const pickPiece = (clientX: number, clientY: number): number | null => {
    const rect = domElement.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) return null;
    ndc.x = ((clientX - rect.left) / rect.width) * 2 - 1;
    ndc.y = -((clientY - rect.top) / rect.height) * 2 + 1;
    camera.updateMatrixWorld();
    root.updateMatrixWorld();
    raycaster.setFromCamera(ndc, camera);
    // 1 ピース = 1 InstancedMesh なので、交差したメッシュから id を引けば足りる
    for (const hit of raycaster.intersectObjects(root.children, false)) {
      const pieceId = hit.object.userData['pieceId'];
      if (typeof pieceId === 'number') return pieceId;
    }
    return null;
  };

  const setSelected = (pieceId: number | null): void => {
    if (selected === pieceId) return;
    selected = pieceId;
    // 選択中のドラッグはピース移動。非選択時だけカメラを旋回・ズームさせる
    orbit.enabled = pieceId === null;
    options.onSelectionChange(pieceId);
  };

  const moveDepth = (dir: 1 | -1): void => {
    if (selected === null) return;
    options.onMove(selected, axisStepVector(currentAxes().depth, dir));
  };

  const onPointerDown = (event: PointerEvent): void => {
    // マウスは主ボタンだけを扱う（タッチ / ペンの button は 0）
    if (event.button !== 0) return;
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });

    if (pointers.size >= 2) {
      // 2 本目が来たらドラッグ移動をやめ、選択中なら奥行き操作に切り替える
      drag = null;
      twoFinger = selected === null ? null : { lastY: averageY(), accumulated: 0 };
      return;
    }

    const pieceId = pickPiece(event.clientX, event.clientY);
    if (pieceId === null) {
      // 何も無い場所 → 選択解除。カメラ旋回はこのあと bubble 段の OrbitCamera が受け取る
      setSelected(null);
      return;
    }
    setSelected(pieceId);
    drag = {
      pointerId: event.pointerId,
      pieceId,
      startX: event.clientX,
      startY: event.clientY,
      axes: currentAxes(),
      pixelsPerVoxel: pixelsPerVoxelAt(pieceId),
      appliedRight: 0,
      appliedUp: 0,
    };
    domElement.setPointerCapture(event.pointerId);
  };

  const onPointerMove = (event: PointerEvent): void => {
    const previous = pointers.get(event.pointerId);
    if (previous === undefined) return;
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });

    if (twoFinger !== null && pointers.size >= 2) {
      const y = averageY();
      // 上へスワイプ（clientY が減る）= 奥へ
      twoFinger.accumulated += twoFinger.lastY - y;
      twoFinger.lastY = y;
      while (twoFinger.accumulated >= TWO_FINGER_PIXELS_PER_STEP) {
        twoFinger.accumulated -= TWO_FINGER_PIXELS_PER_STEP;
        moveDepth(1);
      }
      while (twoFinger.accumulated <= -TWO_FINGER_PIXELS_PER_STEP) {
        twoFinger.accumulated += TWO_FINGER_PIXELS_PER_STEP;
        moveDepth(-1);
      }
      return;
    }

    if (drag === null || drag.pointerId !== event.pointerId) return;
    const dx = event.clientX - drag.startX;
    const dy = event.clientY - drag.startY;
    // 四捨五入なので半マス分ドラッグするまでは動かない = そのままクリックの遊びになる
    const stepsRight = Math.round(dx / drag.pixelsPerVoxel);
    const stepsUp = Math.round(-dy / drag.pixelsPerVoxel);
    const deltaRight = stepsRight - drag.appliedRight;
    const deltaUp = stepsUp - drag.appliedUp;
    if (deltaRight === 0 && deltaUp === 0) return;
    drag.appliedRight = stepsRight;
    drag.appliedUp = stepsUp;
    options.onMove(
      drag.pieceId,
      addVec3(axisStepVector(drag.axes.right, deltaRight), axisStepVector(drag.axes.up, deltaUp)),
    );
  };

  const onPointerUp = (event: PointerEvent): void => {
    if (!pointers.delete(event.pointerId)) return;

    // 手を離したのがどのピースの操作だったかを、状態を消す前に控えておく
    let released: number | null = null;
    if (drag !== null && drag.pointerId === event.pointerId) {
      released = drag.pieceId;
      drag = null;
    } else if (twoFinger !== null && pointers.size < 2) {
      released = selected;
    }
    if (pointers.size < 2) twoFinger = null;
    if (domElement.hasPointerCapture(event.pointerId)) {
      domElement.releasePointerCapture(event.pointerId);
    }
    // 最後の指が離れてから吸い付かせる（2 本目が残っている間はまだ操作中）
    if (released !== null && pointers.size === 0 && options.onRelease) {
      options.onRelease(released);
    }
  };

  const onWheel = (event: WheelEvent): void => {
    // 非選択時はカメラのズーム（OrbitCamera）に任せる
    if (selected === null) return;
    event.preventDefault();
    wheelAccumulated += wheelPixels(event);
    while (wheelAccumulated >= WHEEL_PIXELS_PER_STEP) {
      wheelAccumulated -= WHEEL_PIXELS_PER_STEP;
      moveDepth(1);
    }
    while (wheelAccumulated <= -WHEEL_PIXELS_PER_STEP) {
      wheelAccumulated += WHEEL_PIXELS_PER_STEP;
      moveDepth(-1);
    }
  };

  // capture 段で受けることで、bubble 段の OrbitCamera より先に enabled を切り替えられる
  const captureOptions: AddEventListenerOptions = { capture: true };
  domElement.addEventListener('pointerdown', onPointerDown, captureOptions);
  domElement.addEventListener('pointermove', onPointerMove, captureOptions);
  domElement.addEventListener('pointerup', onPointerUp, captureOptions);
  domElement.addEventListener('pointercancel', onPointerUp, captureOptions);
  domElement.addEventListener('wheel', onWheel, { capture: true, passive: false });

  return {
    selectedPieceId(): number | null {
      return selected;
    },
    select(pieceId: number | null): void {
      setSelected(pieceId);
    },
    moveDepth,
    dispose(): void {
      domElement.removeEventListener('pointerdown', onPointerDown, captureOptions);
      domElement.removeEventListener('pointermove', onPointerMove, captureOptions);
      domElement.removeEventListener('pointerup', onPointerUp, captureOptions);
      domElement.removeEventListener('pointercancel', onPointerUp, captureOptions);
      domElement.removeEventListener('wheel', onWheel, captureOptions);
      pointers.clear();
      drag = null;
      twoFinger = null;
    },
  };
}
