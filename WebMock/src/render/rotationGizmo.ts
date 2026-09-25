// 回転モードのギズモ（Unity 風の回転ハンドル。RULES.md 3.3 の回転操作の見せ方）。
//
// 選択中のピースの周りに X = 赤 / Y = 緑 / Z = 青の 3 本の輪と、その外側に白い輪を出す。
// 白い輪は常にカメラを向き、視線方向の軸まわりの回転を表す（解釈: Unity と同じ割り当て）。
// 輪を掴んだドラッグはその軸まわりだけの回転になる。輪を拾うのはここ（pick）だが、
// 掴んだあとの角度の計算は src/input/pieceInput.ts が持つ。
//
// 描画方針:
//   - ピースに埋もれず見えるよう depthTest を切り、renderOrder を上げて最後に描く
//   - ドローコールは輪 4 本ぶん。回転モード中だけ表示するので常時の負荷は増えない
//   - 掴み判定には見た目より太い「当たり用」の輪を重ねる。material.visible = false なので
//     描画はされず（ドローコールも増えず）、スマホの指でも輪を掴める
//   - ピースのピック（pieceInput の refreshPickTargets）に混ざらないよう userData.pieceId は
//     持たせない。代わりに userData.gizmoAxis を読む

import * as THREE from 'three';

/** 輪の軸。'view' はカメラの視線方向（外周の白い輪）。 */
export type GizmoAxis = 'x' | 'y' | 'z' | 'view';

/** 輪の色。X = 赤 / Y = 緑 / Z = 青 / 外周 = 白。 */
const AXIS_COLORS: Readonly<Record<GizmoAxis, number>> = {
  x: 0xff5a5a,
  y: 0x5ce06a,
  z: 0x5a9bff,
  view: 0xf0f4ff,
};

/** 掴んでいる輪の強調色（オレンジ寄り）。 */
const ACTIVE_COLOR = 0xffa53a;

/**
 * 管の太さ（輪の半径に対する比）。
 * 見た目は細く、当たり判定は太く取る。当たり用が輪の半径の 14 % なので、
 * 画面上で輪が 60 px でも 8 px 幅の帯になり、指でも掴める（指示書の注意）。
 */
const RING_TUBE_RATIO = 0.05;
const PICK_TUBE_RATIO = 0.14;

/** 外周（白い輪）は他の 3 本より少し大きい。 */
const VIEW_RING_SCALE = 1.18;

/** 不透明度。通常 / 掴んでいる輪 / 掴んでいない輪。 */
const BASE_OPACITY = 0.8;
const ACTIVE_OPACITY = 1;
const DIM_OPACITY = 0.32;

/** ピースやスナップ候補より後に描く。 */
const RENDER_ORDER = 999;


export type RotationGizmo = {
  /** シーンに add するルート。位置と大きさは place で決める。 */
  readonly object: THREE.Object3D;
  /** 表示位置と半径を決める。回転モードに入るたび / ピースが動くたびに呼ぶ。 */
  place(center: THREE.Vector3, radius: number): void;
  setVisible(visible: boolean): void;
  /** 今表示中か。 */
  isVisible(): boolean;
  /** 掴んでいる輪を強調する（null で解除）。 */
  setActive(axis: GizmoAxis | null): void;
  /** 外周の白い輪をカメラへ向け直す。毎フレーム呼ぶ。 */
  update(camera: THREE.Camera): void;
  /** 画面座標のレイで輪を拾う。掴めなければ null。 */
  pick(raycaster: THREE.Raycaster): GizmoAxis | null;
  /** 輪の中心（ワールド座標）を target に書いて返す。掴んだ輪まわりの角度計算に使う。 */
  center(target: THREE.Vector3): THREE.Vector3;
  dispose(): void;
};

/** 輪 1 本ぶん。表示用と当たり用の 2 枚を同じ姿勢で重ねる。 */
type Ring = {
  readonly axis: GizmoAxis;
  readonly mesh: THREE.Mesh;
  readonly pick: THREE.Mesh;
  readonly material: THREE.MeshBasicMaterial;
  /** 強調を解いたときに戻す色。 */
  readonly color: THREE.Color;
};

/**
 * 輪の姿勢。TorusGeometry は XY 平面に寝ていて軸が +Z なので、
 * X の輪は Y まわりに、Y の輪は X まわりに 90 度倒す（Z の輪はそのまま）。
 * 'view' は毎フレーム update でカメラの姿勢を写すのでここでは置かない。
 */
function orientRing(object: THREE.Object3D, axis: GizmoAxis): void {
  if (axis === 'x') object.rotation.set(0, Math.PI / 2, 0);
  else if (axis === 'y') object.rotation.set(Math.PI / 2, 0, 0);
}

/**
 * 回転ギズモを作る。半径 1 で組んでおき、place のスケールで実寸に合わせる
 * （管の太さも一緒に比例するので、どのピースでも見た目の比率が変わらない）。
 */
export function createRotationGizmo(): RotationGizmo {
  const object = new THREE.Group();
  object.visible = false;
  // 輪はピースの手前に描くので、three の透明ソートに任せず自前で順番を決める
  object.renderOrder = RENDER_ORDER;

  const ringGeometry = new THREE.TorusGeometry(1, RING_TUBE_RATIO, 8, 64);
  const pickGeometry = new THREE.TorusGeometry(1, PICK_TUBE_RATIO, 6, 32);
  // 当たり用は描かない。material.visible = false はレンダラだけが見るフラグで、
  // レイキャストは Object3D.visible / material.visible を見ないのでピックには効き続ける
  const pickMaterial = new THREE.MeshBasicMaterial({ visible: false });

  const rings: Ring[] = (['x', 'y', 'z', 'view'] as const).map((axis): Ring => {
    const color = new THREE.Color(AXIS_COLORS[axis]);
    const material = new THREE.MeshBasicMaterial({
      color: color.clone(),
      transparent: true,
      opacity: BASE_OPACITY,
      depthTest: false,
      depthWrite: false,
      toneMapped: false,
    });
    const mesh = new THREE.Mesh(ringGeometry, material);
    mesh.renderOrder = RENDER_ORDER;
    mesh.frustumCulled = false;
    const pick = new THREE.Mesh(pickGeometry, pickMaterial);
    pick.frustumCulled = false;
    // ピースのピックに混ざらないよう pieceId は付けない（指示書の注意）
    pick.userData['gizmoAxis'] = axis;
    orientRing(mesh, axis);
    orientRing(pick, axis);
    if (axis === 'view') {
      mesh.scale.setScalar(VIEW_RING_SCALE);
      pick.scale.setScalar(VIEW_RING_SCALE);
    }
    object.add(mesh, pick);
    return { axis, mesh, pick, material, color };
  });

  const pickTargets = rings.map((ring): THREE.Mesh => ring.pick);
  const hits: THREE.Intersection[] = [];
  const viewRings = rings.filter((ring): boolean => ring.axis === 'view');
  // 交点でどの輪を掴んだか決めるための一時ベクトル（レイ 1 本ごとに使い回す）
  const hitAxis = new THREE.Vector3();

  /**
   * その交差が「どれだけ正面を向いた輪か」。1 = 正対（円に見える）、0 = 真横（線に潰れている）。
   * TorusGeometry の軸は +Z なので、輪の姿勢を掛けて軸をワールドへ出す
   * （ルートは平行移動と等倍スケールだけなので mesh.quaternion がそのまま世界の姿勢）。
   */
  const facingOf = (hit: THREE.Intersection, raycaster: THREE.Raycaster): number => {
    hitAxis.set(0, 0, 1).applyQuaternion(hit.object.quaternion);
    return Math.abs(hitAxis.dot(raycaster.ray.direction));
  };

  return {
    object,
    place(center: THREE.Vector3, radius: number): void {
      object.position.copy(center);
      object.scale.setScalar(radius);
    },
    setVisible(visible: boolean): void {
      object.visible = visible;
    },
    isVisible(): boolean {
      return object.visible;
    },
    setActive(axis: GizmoAxis | null): void {
      for (const ring of rings) {
        const active = axis !== null && ring.axis === axis;
        ring.material.color.set(active ? ACTIVE_COLOR : ring.color);
        ring.material.opacity =
          axis === null ? BASE_OPACITY : active ? ACTIVE_OPACITY : DIM_OPACITY;
      }
    },
    update(camera: THREE.Camera): void {
      if (!object.visible) return;
      // ルートは平行移動と等倍スケールだけなので、カメラの姿勢をそのまま写せば画面と平行になる
      for (const ring of viewRings) {
        ring.mesh.quaternion.copy(camera.quaternion);
        ring.pick.quaternion.copy(camera.quaternion);
      }
    },
    pick(raycaster: THREE.Raycaster): GizmoAxis | null {
      if (!object.visible) return null;
      object.updateMatrixWorld();
      hits.length = 0;
      raycaster.intersectObjects(pickTargets, false, hits);
      // 重なっている輪の中からは「正面を向いている輪」を選ぶ。
      // 解釈: 線に潰れて見える輪はどこを掴んでも見た目が同じで、しかも画面上では
      // 輪の内側を横切る帯になるため、手前の交差をそのまま採ると正対した輪を掴めなくなる。
      // 円に見えている輪の方が「その輪に沿ってドラッグする」意図に合うので優先する
      // （差が無ければ手前を採る。intersectObjects は手前から並べて返す）。
      let chosen = hits[0];
      let bestFacing = chosen === undefined ? 0 : facingOf(chosen, raycaster);
      for (const hit of hits) {
        const facing = facingOf(hit, raycaster);
        if (facing > bestFacing + 1e-3) {
          bestFacing = facing;
          chosen = hit;
        }
      }
      const axis = chosen?.object.userData['gizmoAxis'];
      hits.length = 0;
      return axis === 'x' || axis === 'y' || axis === 'z' || axis === 'view' ? axis : null;
    },
    center(target: THREE.Vector3): THREE.Vector3 {
      return target.copy(object.position);
    },
    dispose(): void {
      object.removeFromParent();
      object.clear();
      ringGeometry.dispose();
      pickGeometry.dispose();
      pickMaterial.dispose();
      for (const ring of rings) ring.material.dispose();
    },
  };
}
