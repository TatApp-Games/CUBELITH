// ポインタ / タッチ入力をピース操作に変換する（SPEC.md 3.3）。
// クリック / タップで選択、ドラッグでボクセル単位の移動、ホイールで奥行き移動、
// 2 本指で 90 度回転とピンチズーム。
// 選択中はカメラの旋回を止め、非選択時のドラッグはカメラに任せる（排他）。
//
// 2 本指の割り当て（解釈）: SPEC.md 3.3 は回転も奥行き移動も「2 本指スワイプ **または** UI ボタン」を
// 認めている。両方を 2 本指に載せると区別できないので、**2 本指は回転（スワイプ / ひねり）と
// ピンチズームに割り当て、奥行き移動は HUD の「奥へ / 手前へ」ボタンとホイールに寄せる**。
// 認識そのものは Three.js に依存しない twoFingerGesture.ts が持つ。

import * as THREE from 'three';
import { addVec3, type Axis, type Vec3 } from '../core/grid';
import type { OrbitCamera } from '../render/camera';
import { axisStepVector, dragAxes, type DragAxes } from './axisMapping';
import { pickSampleOffsets } from './pickSamples';
import { createTwoFingerGesture, type Point } from './twoFingerGesture';

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

/**
 * 近接ピックの許容半径（CSS ピクセル）。中心のレイが外れたときだけ、この半径内へずらしたレイを撃つ。
 *
 * 解釈: ボクセルには 0.04 マス分の溝があり（VOXEL_SIZE = 0.96）、細いピースの縁やスマホで小さく
 * 見えているピースは 1 本のレイでは簡単に外れる。半径はポインタの種類で変える —
 * 指の接触面は広く狙いも粗いので touch は 16 px、マウス / ペンは狙いが正確なので 8 px。
 * どちらもボクセル 1 個の見かけの大きさ（MIN_PIXELS_PER_VOXEL = 14 px 前後）より小さいので、
 * 隣のピースを誤って拾うより先に、狙ったピースの隙間を埋める効き方になる。
 */
const TOUCH_PICK_RADIUS_PX = 16;
const PRECISE_PICK_RADIUS_PX = 8;

/** サンプル点は半径ごとに固定なので、起動時に 1 度だけ作って使い回す。 */
const TOUCH_PICK_OFFSETS = pickSampleOffsets(TOUCH_PICK_RADIUS_PX);
const PRECISE_PICK_OFFSETS = pickSampleOffsets(PRECISE_PICK_RADIUS_PX);

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
  /**
   * そのピースが固定中か。固定中のピースは「選択はできるが動かせない」
   * （固定解除するために選ぶ必要があるので、選択だけは通す）。
   */
  readonly isLocked?: (pieceId: number) => boolean;
  /** 選択が変わったとき（解除は null）。 */
  readonly onSelectionChange: (pieceId: number | null) => void;
  /** ピースをグリッド上で delta マス動かす要求。 */
  readonly onMove: (pieceId: number, delta: Vec3) => void;
  /**
   * ピースを軸まわりに 90 度回す要求（2 本指ジェスチャ）。
   * 軸は画面基準のジェスチャをカメラの向きでグリッド軸へ写したもの。
   */
  readonly onRotate?: (pieceId: number, axis: Axis, dir: 1 | -1) => void;
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
  // カメラ基底の取り出し用。呼ばれるたびに Vector3 を作らないよう使い回す
  const cameraRight = new THREE.Vector3();
  const cameraUp = new THREE.Vector3();
  const cameraForward = new THREE.Vector3();
  /**
   * レイキャストの対象と結果。1 回の pointerdown で十数本のレイを撃つので、
   * 配列は作り直さず中身だけ入れ替えて使い回す（GC を増やさない）。
   * root.children には発光コアや稜線も混ざるため、対象はピース本体だけに絞る。
   */
  const pickTargets: THREE.Object3D[] = [];
  const pickHits: THREE.Intersection[] = [];

  let selected: number | null = null;
  let drag: DragState | null = null;

  /** 固定中なら true。isLocked を渡さなければ常に false（固定の概念が無い呼び出し側）。 */
  const isLocked = (pieceId: number): boolean => options.isLocked?.(pieceId) ?? false;
  let wheelAccumulated = 0;

  /** 追跡中のポインタ（主ボタン / 指のみ）。 */
  const pointers = new Map<number, Point>();

  // 2 本指ジェスチャ。ピースを選んでいる間だけ使う（未選択時のピンチはカメラ側が拾う）
  const gesture = createTwoFingerGesture();
  let gestureActive = false;
  /**
   * 操作が終わったピース。最後の指が離れた時点で onRelease に流す。
   * 2 本指の操作は指が 1 本ずつ離れるので、1 本目が離れた時点では確定させられない。
   */
  let pendingRelease: number | null = null;

  /**
   * 追跡中のうち先に触れた 2 本を、触れた順で返す。
   * Map は挿入順を保つので、途中で 3 本目が来ても基準の 2 本は入れ替わらない。
   * 順が入れ替わるとひねりの向きが反転するため、順序の安定はここで担保する。
   */
  const firstTwoPointers = (): [Point, Point] | null => {
    const list = [...pointers.values()];
    const a = list[0];
    const b = list[1];
    return a && b ? [a, b] : null;
  };

  /**
   * 現在のカメラからドラッグ軸を作る。
   * 解釈: ピースのルートは平行移動しかしていないので、ワールド軸とグリッド軸は一致する。
   */
  const currentAxes = (): DragAxes => {
    camera.updateMatrixWorld();
    cameraRight.setFromMatrixColumn(camera.matrixWorld, 0);
    cameraUp.setFromMatrixColumn(camera.matrixWorld, 1);
    // カメラは自身の -Z を向く
    cameraForward.setFromMatrixColumn(camera.matrixWorld, 2).negate();
    return dragAxes(cameraRight, cameraUp, cameraForward);
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

  /** root.children から、ピース本体（userData.pieceId を持つもの）だけを対象配列へ集め直す。 */
  const refreshPickTargets = (): void => {
    pickTargets.length = 0;
    for (const child of root.children) {
      if (typeof child.userData['pieceId'] === 'number') pickTargets.push(child);
    }
  };

  /**
   * 画面座標のピースを拾う。何も無ければ null。
   *
   * 中心のレイが当たればそれを採用し、外れたときだけ近接サンプル（pickSampleOffsets）を撃つ。
   * サンプルが複数当たったときはレイ原点に最も近い交差のピースを選ぶので、
   * 「重なっているときは手前のピースを選ぶ」という中心レイの挙動と揃う。
   */
  const pickPiece = (clientX: number, clientY: number, pointerType: string): number | null => {
    const rect = domElement.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) return null;
    camera.updateMatrixWorld();
    root.updateMatrixWorld();
    refreshPickTargets();
    if (pickTargets.length === 0) return null;

    // 解釈: pointerType は 'touch' / 'mouse' / 'pen' 以外（未対応ブラウザの ''）もあり得るので、
    // touch だけを広い半径にして、それ以外は狙いが正確な側へ倒す
    const offsets = pointerType === 'touch' ? TOUCH_PICK_OFFSETS : PRECISE_PICK_OFFSETS;
    let nearestId: number | null = null;
    let nearestDistance = Number.POSITIVE_INFINITY;
    for (let i = 0; i < offsets.length; i += 1) {
      const offset = offsets[i];
      if (offset === undefined) continue;
      ndc.x = ((clientX + offset.dx - rect.left) / rect.width) * 2 - 1;
      ndc.y = -((clientY + offset.dy - rect.top) / rect.height) * 2 + 1;
      raycaster.setFromCamera(ndc, camera);
      // intersectObjects は結果を手前から並べて返すので、先頭だけ見れば足りる
      pickHits.length = 0;
      raycaster.intersectObjects(pickTargets, false, pickHits);
      const hit = pickHits[0];
      if (hit === undefined) continue;
      const pieceId = hit.object.userData['pieceId'];
      if (typeof pieceId !== 'number') continue;
      // 先頭は必ず中心のレイ。当たったならずらしたレイを撃つまでもない
      if (i === 0) return pieceId;
      if (hit.distance < nearestDistance) {
        nearestDistance = hit.distance;
        nearestId = pieceId;
      }
    }
    return nearestId;
  };

  const setSelected = (pieceId: number | null): void => {
    if (selected === pieceId) return;
    selected = pieceId;
    // 選択中のドラッグはピース移動。非選択時だけカメラを旋回・ズームさせる
    orbit.enabled = pieceId === null;
    options.onSelectionChange(pieceId);
  };

  const moveDepth = (dir: 1 | -1): void => {
    if (selected === null || isLocked(selected)) return;
    options.onMove(selected, axisStepVector(currentAxes().depth, dir));
  };

  /**
   * 2 本指ジェスチャを 1 回分処理する。
   *
   * 回転の軸は画面基準（Yaw = 画面の上、Pitch = 画面の右、Roll = 画面の奥）で決め、
   * dragAxes でグリッド軸へ写す。向きは「見たまま回る」ように取る:
   * 右へスワイプ → 手前の面が右へ（画面の上軸まわりに +90 度）、
   * 上へスワイプ → 上の面が奥へ（画面の右軸まわりに −90 度）、
   * 時計回りにひねる → 画面上でも時計回り（画面の奥軸まわりに +90 度）。
   */
  const applyTwoFinger = (a: Point, b: Point): void => {
    const action = gesture.update(a, b);
    if (action.kind === 'none') return;
    if (action.kind === 'zoom') {
      // 選択中は orbit.enabled = false なので、ズームだけ直接掛ける
      orbit.zoomBy(action.scale);
      return;
    }
    const pieceId = selected;
    const onRotate = options.onRotate;
    // ズーム（上の分岐）は固定中でも効かせる。回るのは固定していないピースだけ
    if (pieceId === null || onRotate === undefined || isLocked(pieceId)) return;
    const axes = currentAxes();
    const step =
      action.gesture === 'yaw'
        ? axes.up
        : action.gesture === 'pitch'
          ? axes.right
          : axes.depth;
    const screenSign = action.gesture === 'pitch' ? -action.dir : action.dir;
    onRotate(pieceId, step.axis, screenSign * step.sign > 0 ? 1 : -1);
  };

  const onPointerDown = (event: PointerEvent): void => {
    // マウスは主ボタンだけを扱う（タッチ / ペンの button は 0）
    if (event.button !== 0) return;
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });

    if (pointers.size >= 2) {
      // 2 本目が来たらドラッグ移動をやめ、選択中なら 2 本指ジェスチャに切り替える
      drag = null;
      const pair = firstTwoPointers();
      gestureActive = selected !== null && pair !== null;
      if (gestureActive && pair) gesture.reset(pair[0], pair[1]);
      return;
    }

    const pieceId = pickPiece(event.clientX, event.clientY, event.pointerType);
    if (pieceId === null) {
      // 何も無い場所 → 選択解除。カメラ旋回はこのあと bubble 段の OrbitCamera が受け取る
      setSelected(null);
      return;
    }
    setSelected(pieceId);
    // 固定中のピースは選ぶだけ。ドラッグ移動は始めない
    if (isLocked(pieceId)) return;
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

    if (gestureActive && pointers.size >= 2) {
      const pair = firstTwoPointers();
      if (pair) applyTwoFinger(pair[0], pair[1]);
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
    if (drag !== null && drag.pointerId === event.pointerId) {
      pendingRelease = drag.pieceId;
      drag = null;
    } else if (gestureActive && pointers.size < 2) {
      pendingRelease = selected;
    }
    if (pointers.size < 2) gestureActive = false;
    if (domElement.hasPointerCapture(event.pointerId)) {
      domElement.releasePointerCapture(event.pointerId);
    }
    // 最後の指が離れてから吸い付かせる（指が残っている間はまだ操作中）
    if (pointers.size === 0 && pendingRelease !== null) {
      const released = pendingRelease;
      pendingRelease = null;
      // 固定中のピースはスナップさせない（吸い付いて動いてしまわないように）
      if (options.onRelease && !isLocked(released)) options.onRelease(released);
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
      gestureActive = false;
      pendingRelease = null;
    },
  };
}
