// ポインタ / タッチ入力をピース操作に変換する（SPEC.md 3.3）。
// クリック / タップで選択、ドラッグでボクセル単位の移動、ホイールで奥行き移動、
// 2 本指で 90 度回転とピンチズーム。
// 選択中はカメラの旋回を止め、非選択時のドラッグはカメラに任せる（排他）。
//
// 回転モード（setRotateMode）の間だけ、選択中のピースへのドラッグは移動ではなく連続回転になる。
// 90 度単位に縛らずに回して見せ（onFreeRotate）、指を離した時点で最寄りの向きへ確定させる
// （onFreeRotateEnd）。確定の計算は input/freeRotation.ts、表示は render/pieces.ts が持つ。
//
// 回転モード中は回転ギズモ（render/rotationGizmo.ts）の輪も掴める。輪を掴んだドラッグは
// その軸まわりだけの回転になり（X / Y / Z はワールド軸、外周の白い輪はカメラの視線方向）、
// 輪の外を掴んだドラッグは従来どおりカメラ基底まわりのトラックボール回転になる。
//
// 2 本指の割り当て（解釈）: SPEC.md 3.3 は回転も奥行き移動も「2 本指スワイプ **または** UI ボタン」を
// 認めている。両方を 2 本指に載せると区別できないので、**2 本指は回転（スワイプ / ひねり）と
// ピンチズームに割り当て、奥行き移動はホイールに寄せる**（HUD の奥行きボタンは廃止済み）。
// 認識そのものは Three.js に依存しない twoFingerGesture.ts が持つ。

import * as THREE from 'three';
import { addVec3, type Axis, type Vec3 } from '../core/grid';
import type { OrbitCamera } from '../render/camera';
import type { GizmoAxis } from '../render/rotationGizmo';
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
 * 回転モードのドラッグ感度（ドラッグ 1 px あたりの回転角・度）。
 * 90 度回すのに 225 px なので、スマホの短辺（〜390 px）の中で 1 回転の 1/4 を
 * 無理なく越えられ、かつ指の震えでスナップ先が変わらない程度に鈍い。
 */
const ROTATE_DEGREES_PER_PIXEL = 0.4;

/**
 * 掴んだ輪の角度を「平面への射影」で測れるかの境目（レイと輪の軸の内積の絶対値）。
 *
 * 輪をほぼ真横から見ている（＝画面上で線に潰れている）ときはレイが輪の平面とほぼ平行で、
 * 交点が遠くへ飛んで角度が暴れる。そのときだけ「接線方向へのドラッグ量 × 感度」に切り替える
 * （指示書が認めている簡易版）。0.25 は輪の面が視線から 75 度ほど傾いたあたり。
 */
const GIZMO_PLANE_MIN_DOT = 0.25;

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
  /**
   * パズルの回転が「あり」なら true（省略時は true）。false のときは 2 本指の 90 度回転を行わず、
   * 回転モードにも入らない（setRotateMode(true) を無視する）。ピンチズームは残す。
   */
  readonly allowRotation?: boolean;
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
  /**
   * 回転モードのドラッグ中、表示だけの自由回転が変わったとき（90 度単位に縛られない）。
   * 論理上の配置はまだ変えない。渡すクォータニオンは使い回しなので、
   * 受け取り側で保持するなら中身を写すこと。
   */
  readonly onFreeRotate?: (pieceId: number, quaternion: THREE.Quaternion) => void;
  /**
   * 回転モードのドラッグから指を離したとき。最寄りの向き（24 通り）へ確定させるきっかけ。
   * このときは onRelease（移動のマグネットスナップ）を呼ばない。
   */
  readonly onFreeRotateEnd?: (pieceId: number, quaternion: THREE.Quaternion) => void;
  /**
   * 回転モードの pointerdown で回転ギズモの輪を拾う（render/rotationGizmo.ts の pick）。
   * 掴めなければ null。ピース本体のピックより先に呼ぶので、輪がピースの外にはみ出していても
   * 掴んだ瞬間に選択が外れない。
   */
  readonly pickGizmo?: (raycaster: THREE.Raycaster) => GizmoAxis | null;
  /**
   * 回転ギズモの輪の中心（ワールド座標）を target に書いて返す。掴んだ輪まわりの角度計算に使う。
   * 返せないときは null（その場合はトラックボール回転にフォールバックする）。
   */
  readonly gizmoCenter?: (target: THREE.Vector3) => THREE.Vector3 | null;
  /** 掴んでいる輪が変わったとき（離したら null）。ギズモの強調表示に使う。 */
  readonly onGizmoAxisChange?: (axis: GizmoAxis | null) => void;
};

export type PieceInput = {
  /** 現在アクティブなピース id。無ければ null。 */
  selectedPieceId(): number | null;
  /** 外（UI など）から選択を変える。 */
  select(pieceId: number | null): void;
  /** アクティブなピースを奥行き方向に 1 マス動かす（+1 = カメラから遠ざかる）。 */
  moveDepth(dir: 1 | -1): void;
  /**
   * 回転モードの出入り。オンの間、選択中のピースへのドラッグは移動ではなく連続回転になる。
   * 選択が無い / 固定中のピースではオンにできないので、結果は rotateMode() で確かめる。
   */
  setRotateMode(enabled: boolean): void;
  /** 今が回転モードか。 */
  rotateMode(): boolean;
  /** イベントリスナを外す。 */
  dispose(): void;
};

/** ドラッグ 1 本分の状態。開始時に軸と感度を固定し、途中でカメラが動いてもぶれないようにする。 */
type MoveDrag = {
  readonly kind: 'move';
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

/**
 * 回転モードのドラッグ 1 本分の状態。
 * current は「開始時の姿勢 base にドラッグ量ぶんの増分を掛けたもの」を毎回作り直して入れる
 * （差分を積み重ねると誤差が乗るため）。
 */
type RotateDrag = {
  readonly kind: 'rotate';
  readonly pointerId: number;
  readonly pieceId: number;
  readonly startX: number;
  readonly startY: number;
  readonly base: THREE.Quaternion;
  readonly current: THREE.Quaternion;
  /** 輪を掴んでいるときだけ入る軸拘束の情報。null ならトラックボール回転。 */
  readonly gizmo: GizmoDrag | null;
};

/** 回転ギズモの輪を掴んでいるドラッグ 1 本分。掴んだ時点で軸と測り方を固定する。 */
type GizmoDrag = {
  readonly axis: GizmoAxis;
  /** 回転軸（ワールド座標・正規化済み）。'view' はカメラの視線方向。 */
  readonly vector: THREE.Vector3;
  /** 輪の中心（ワールド座標）。 */
  readonly center: THREE.Vector3;
  /** 角度の測り方。'plane' = 輪の平面へ射影、'tangent' = 接線方向へのドラッグ量。 */
  readonly mode: 'plane' | 'tangent';
  /** 輪の平面の基底（mode = 'plane'）。u × v = vector の右手系なので角度の向きも軸に揃う。 */
  readonly u: THREE.Vector3;
  readonly v: THREE.Vector3;
  /** 画面上の接線方向（mode = 'tangent'。y は上向き・単位ベクトル）。 */
  readonly tangentX: number;
  readonly tangentY: number;
  /** 直前に測った角度（mode = 'plane'）。 */
  lastAngle: number;
  /** 掴んでからの累積回転角（ラジアン）。輪を何周回しても足し込まれる。 */
  total: number;
};

type DragState = MoveDrag | RotateDrag;

/** 角度差を (-π, π] に畳む。輪を何周回しても累積が飛ばないようにする。 */
function wrapAngle(angle: number): number {
  const wrapped = (angle + Math.PI) % (Math.PI * 2);
  return (wrapped < 0 ? wrapped + Math.PI * 2 : wrapped) - Math.PI;
}


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
  /** 回転モード（HUD の「回転 / 回転解除」で切り替える）。 */
  let rotateModeOn = false;
  // 回転の増分を組み立てる一時オブジェクト。ドラッグ中は毎フレーム通るので使い回す
  const yawStep = new THREE.Quaternion();
  const pitchStep = new THREE.Quaternion();
  // 輪を掴んだ回転（軸拘束）の一時オブジェクト。こちらも毎フレーム通る
  const axisStep = new THREE.Quaternion();
  const ringPlane = new THREE.Plane();
  const ringPoint = new THREE.Vector3();
  const projected = new THREE.Vector3();

  /** 固定中なら true。isLocked を渡さなければ常に false（固定の概念が無い呼び出し側）。 */
  const isLocked = (pieceId: number): boolean => options.isLocked?.(pieceId) ?? false;
  const allowRotation = options.allowRotation ?? true;
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
   * 画面座標から raycaster にレイを張る。キャンバスの大きさが 0 なら false（張れない）。
   * ギズモのピックと、輪の平面へポインタを射影するのに使う。
   */
  const setRayFrom = (clientX: number, clientY: number): boolean => {
    const rect = domElement.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) return false;
    camera.updateMatrixWorld();
    ndc.x = ((clientX - rect.left) / rect.width) * 2 - 1;
    ndc.y = -((clientY - rect.top) / rect.height) * 2 + 1;
    raycaster.setFromCamera(ndc, camera);
    return true;
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

  /**
   * 走っている回転ドラッグをその場で確定させる（指を離す以外の理由で終わるとき）。
   * ここを通さないと、表示だけねじれたピースが残る。
   */
  const commitRotateDrag = (): void => {
    if (drag === null || drag.kind !== 'rotate') return;
    const finished = drag;
    drag = null;
    // 掴んでいた輪の強調を先に解く（確定でピースが描き直される前に戻す）
    if (finished.gizmo !== null) options.onGizmoAxisChange?.(null);
    options.onFreeRotateEnd?.(finished.pieceId, finished.current);
  };

  const setSelected = (pieceId: number | null): void => {
    if (selected === pieceId) return;
    // 選択が変わったら回転モードは自動で解除する（回転は選んだピースに紐づく操作）
    commitRotateDrag();
    rotateModeOn = false;
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
    // 回転モードでは 2 本指の 90 度回転を無効にする（回転はドラッグの連続回転へ一本化する）。
    // 回転なしの難易度でも 90 度回転は行わない。ズーム（上の分岐）だけはどちらでも効かせる
    if (rotateModeOn || !allowRotation) return;
    const pieceId = selected;
    const onRotate = options.onRotate;
    // ズームは固定中でも効かせる。回るのは固定していないピースだけ
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

  /**
   * 掴んだ輪から軸拘束の状態を作る。中心が取れない / レイを張れないときは null を返し、
   * 呼び出し側はトラックボール回転へフォールバックする。
   *
   * 角度の測り方はここで 1 回だけ決める。ドラッグ中はカメラが止まっている
   * （選択中は orbit.enabled = false）ので、途中で測り方が変わることはない。
   */
  const beginGizmoDrag = (axis: GizmoAxis, clientX: number, clientY: number): GizmoDrag | null => {
    const center = options.gizmoCenter?.(new THREE.Vector3());
    if (!center) return null;
    if (!setRayFrom(clientX, clientY)) return null;
    const vector = new THREE.Vector3();
    if (axis === 'x') vector.set(1, 0, 0);
    else if (axis === 'y') vector.set(0, 1, 0);
    else if (axis === 'z') vector.set(0, 0, 1);
    // 外周の白い輪は視線方向の軸。カメラは自身の -Z を向く
    else vector.setFromMatrixColumn(camera.matrixWorld, 2).negate().normalize();

    const u = new THREE.Vector3();
    const v = new THREE.Vector3();
    if (Math.abs(raycaster.ray.direction.dot(vector)) >= GIZMO_PLANE_MIN_DOT) {
      // 輪の平面が十分こちらを向いている。掴んだ点をそのまま追いかけられる
      // 軸と平行でない適当なベクトルから、右手系の基底 (u, v, vector) を作る
      u.set(Math.abs(vector.x) < 0.9 ? 1 : 0, Math.abs(vector.x) < 0.9 ? 0 : 1, 0)
        .cross(vector)
        .normalize();
      v.copy(vector).cross(u);
      const gizmo: GizmoDrag = {
        axis,
        vector,
        center,
        mode: 'plane',
        u,
        v,
        tangentX: 0,
        tangentY: 0,
        lastAngle: 0,
        total: 0,
      };
      // 掴んだ点の角度を基準にする（ここからの差分だけを積む）
      const angle = ringAngle(gizmo, clientX, clientY);
      if (angle !== null) gizmo.lastAngle = angle;
      return gizmo;
    }

    // 輪が線に潰れて見えている。画面上での「輪の接線方向」へのドラッグ量を角度にする。
    // 接線は軸の画面上の向きに直交する側（軸が画面の上を向いていれば、右へのドラッグで回る）
    const rect = domElement.getBoundingClientRect();
    projected.copy(center).project(camera);
    const baseX = projected.x * rect.width;
    const baseY = projected.y * rect.height;
    projected.copy(center).add(vector).project(camera);
    let dirX = projected.x * rect.width - baseX;
    let dirY = projected.y * rect.height - baseY;
    const length = Math.hypot(dirX, dirY);
    // 軸が真正面を向いていればここには来ないが、念のため（縮退時は横方向へ倒す）
    if (length < 1e-6) {
      dirX = 0;
      dirY = 1;
    } else {
      dirX /= length;
      dirY /= length;
    }
    return {
      axis,
      vector,
      center,
      mode: 'tangent',
      u,
      v,
      tangentX: dirY,
      tangentY: -dirX,
      lastAngle: 0,
      total: 0,
    };
  };

  /**
   * 掴んだ輪の平面へポインタを射影して、中心まわりの角度（ラジアン）を測る。
   * レイが平面と交わらなければ null。
   */
  const ringAngle = (gizmo: GizmoDrag, clientX: number, clientY: number): number | null => {
    if (!setRayFrom(clientX, clientY)) return null;
    ringPlane.setFromNormalAndCoplanarPoint(gizmo.vector, gizmo.center);
    if (raycaster.ray.intersectPlane(ringPlane, ringPoint) === null) return null;
    ringPoint.sub(gizmo.center);
    return Math.atan2(ringPoint.dot(gizmo.v), ringPoint.dot(gizmo.u));
  };

  /** 輪を掴んでいるドラッグの累積回転角を更新する。 */
  const updateGizmoDrag = (state: RotateDrag, gizmo: GizmoDrag, x: number, y: number): void => {
    if (gizmo.mode === 'tangent') {
      // 画面の y は下向きなので、上へのドラッグを +y に直してから接線へ射影する
      const dx = x - state.startX;
      const dy = -(y - state.startY);
      gizmo.total =
        (dx * gizmo.tangentX + dy * gizmo.tangentY) *
        THREE.MathUtils.degToRad(ROTATE_DEGREES_PER_PIXEL);
      return;
    }
    const angle = ringAngle(gizmo, x, y);
    if (angle === null) return;
    // 1 周をまたいでも連続になるよう、差分を畳んでから積む
    gizmo.total += wrapAngle(angle - gizmo.lastAngle);
    gizmo.lastAngle = angle;
  };

  /**
   * 回転モードのドラッグ量から回転を作って current に入れる。
   * 輪を掴んでいればその軸まわりだけ、掴んでいなければカメラ基底まわりのトラックボール回転。
   *
   * トラックボールの向きの割り当ては 2 本指の 90 度回転（applyTwoFinger）と同じ
   * 「見たまま回る」考え方: 右へドラッグ → カメラの上ベクトルまわりに +、
   * 上へドラッグ → カメラの右ベクトルまわりに −。
   * どちらも毎回 base から作り直すので、ドラッグを往復させても誤差が溜まらない。
   */
  const updateRotateDrag = (state: RotateDrag, clientX: number, clientY: number): void => {
    if (state.gizmo !== null) {
      // 輪を掴んでいる間はその軸まわりだけ。ワールド軸まわりの回転なので左から重ねる
      updateGizmoDrag(state, state.gizmo, clientX, clientY);
      axisStep.setFromAxisAngle(state.gizmo.vector, state.gizmo.total);
      state.current.copy(axisStep).multiply(state.base);
      return;
    }
    const dx = clientX - state.startX;
    const dy = clientY - state.startY;
    camera.updateMatrixWorld();
    cameraRight.setFromMatrixColumn(camera.matrixWorld, 0).normalize();
    cameraUp.setFromMatrixColumn(camera.matrixWorld, 1).normalize();
    yawStep.setFromAxisAngle(cameraUp, THREE.MathUtils.degToRad(dx * ROTATE_DEGREES_PER_PIXEL));
    // 画面の y は下向きなので、上へのドラッグ（dy < 0）がそのまま右軸まわりの − になる
    pitchStep.setFromAxisAngle(
      cameraRight,
      THREE.MathUtils.degToRad(dy * ROTATE_DEGREES_PER_PIXEL),
    );
    // カメラ基底（外側の軸）まわりの回転なので、開始時の姿勢へ左から重ねる
    state.current.copy(yawStep).multiply(pitchStep).multiply(state.base);
  };

  const onPointerDown = (event: PointerEvent): void => {
    // マウスは主ボタンだけを扱う（タッチ / ペンの button は 0）
    if (event.button !== 0) return;
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });

    if (pointers.size >= 2) {
      // 2 本目が来たらドラッグ移動をやめ、選択中なら 2 本指ジェスチャに切り替える。
      // 回転ドラッグの途中なら、そこまでの回転を確定させてから切り替える
      commitRotateDrag();
      drag = null;
      const pair = firstTwoPointers();
      gestureActive = selected !== null && pair !== null;
      if (gestureActive && pair) gesture.reset(pair[0], pair[1]);
      return;
    }

    // 回転モード中はギズモの輪を先に拾う。輪はピースの外へはみ出しているので、
    // ピースのピックを先にすると「輪を掴んだのに選択が外れる」ことになる
    if (rotateModeOn && selected !== null && !isLocked(selected)) {
      const axis =
        options.pickGizmo !== undefined && setRayFrom(event.clientX, event.clientY)
          ? options.pickGizmo(raycaster)
          : null;
      if (axis !== null) {
        // 中心が取れなければ gizmo は null（＝トラックボール回転にフォールバックする）
        const gizmo = beginGizmoDrag(axis, event.clientX, event.clientY);
        drag = {
          kind: 'rotate',
          pointerId: event.pointerId,
          pieceId: selected,
          startX: event.clientX,
          startY: event.clientY,
          base: new THREE.Quaternion(),
          current: new THREE.Quaternion(),
          gizmo,
        };
        if (gizmo !== null) options.onGizmoAxisChange?.(axis);
        domElement.setPointerCapture(event.pointerId);
        return;
      }
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
    if (rotateModeOn) {
      // 回転モード（別のピースを掴んだなら setSelected が解除しているのでここには来ない）。
      // ギズモの輪の外を掴んだので、ドラッグ全体をトラックボール回転として扱う
      drag = {
        kind: 'rotate',
        pointerId: event.pointerId,
        pieceId,
        startX: event.clientX,
        startY: event.clientY,
        // 直前のドラッグは指を離した時点で確定済みなので、姿勢は毎回そこから始まる
        base: new THREE.Quaternion(),
        current: new THREE.Quaternion(),
        gizmo: null,
      };
      domElement.setPointerCapture(event.pointerId);
      return;
    }
    drag = {
      kind: 'move',
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

    if (drag.kind === 'rotate') {
      // 回転モードは 90 度単位に縛らず、ドラッグ量そのままの角度で回して見せる
      updateRotateDrag(drag, event.clientX, event.clientY);
      options.onFreeRotate?.(drag.pieceId, drag.current);
      return;
    }

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
      if (drag.kind === 'rotate') {
        // 回転モードは離した時点で最寄りの向きへ確定させる。移動のスナップ（onRelease）は通さない
        updateRotateDrag(drag, event.clientX, event.clientY);
        commitRotateDrag();
      } else {
        pendingRelease = drag.pieceId;
        drag = null;
      }
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
      // 固定中のピースはスナップさせない（吸い付いて動いてしまわないように）。
      // 回転モード中も移動していないので、移動のマグネットスナップは掛けない
      if (options.onRelease && !isLocked(released) && !rotateModeOn) options.onRelease(released);
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
    setRotateMode(enabled: boolean): void {
      // 選択が無い / 固定中のピースでは回転モードに入らない（00000003_003）。
      // 回転なしの難易度では回転モードそのものが無いので常に入らない
      const next = enabled && allowRotation && selected !== null && !isLocked(selected);
      if (next === rotateModeOn) return;
      // 抜けるときに回転ドラッグが残っていたら、そこまでの回転を確定させる
      commitRotateDrag();
      rotateModeOn = next;
    },
    rotateMode(): boolean {
      return rotateModeOn;
    },
    dispose(): void {
      domElement.removeEventListener('pointerdown', onPointerDown, captureOptions);
      domElement.removeEventListener('pointermove', onPointerMove, captureOptions);
      domElement.removeEventListener('pointerup', onPointerUp, captureOptions);
      domElement.removeEventListener('pointercancel', onPointerUp, captureOptions);
      domElement.removeEventListener('wheel', onWheel, captureOptions);
      pointers.clear();
      drag = null;
      rotateModeOn = false;
      gestureActive = false;
      pendingRelease = null;
    },
  };
}
