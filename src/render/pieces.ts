// ピース群の描画。1 ピース = 1 InstancedMesh（CLAUDE.md 開発ルール 4 / SPEC.md 4 章）。
// 位置・向きの解釈は core に任せ、ここは placedVoxels の結果を行列に落とすだけにする。
//
// ドローコールの内訳（SPEC.md 4 章。N=7 / M=40 でも「ピース数 + α」に収める）:
//   M                ピース本体。ボクセル数によらず 1 ピース 1 回
//   + 1              内部発光コア（glowCores。全ピース分をまとめた 1 つの InstancedMesh）
//   + 1              解答空間の枠（createSolutionFrame の LineSegments）
//   + 1（軽量モードのみ） 稜線の発光（edgeGlow。これも全ピースで 1 本）
// ジオメトリは全ピースで 1 つを共有し、マテリアルだけをピースごとに持つ（色が違うため）。
// すりガラスの屈折パスは three が opaque な物体だけをもう一度描くもので、ピース数には比例しない。

import * as THREE from 'three';
import { equalsVec3 } from '../core/grid';
import { placedVoxels, type Piece, type Placement } from '../core/piece';
import { createEdgeGlow, type EdgeGlow } from './edgeGlow';
import { createGlowCores } from './glowCores';

/**
 * ボクセル 1 個の 1 辺。
 * 解釈: SPEC.md に隙間の指定は無いが、1 のままだと隣接ボクセルの境界が見えずピースの形が
 * 分からないため、わずかに縮めて輪郭が出るようにする。
 */
const VOXEL_SIZE = 0.96;

/**
 * emissive にかける係数。通常時とハイライト時。
 * 解釈: SPEC.md 3.3 の「縁取りかハイライト」は、ポストプロセスを増やさない方針（CLAUDE.md 開発ルール 5）に
 * 合わせて emissive を上げる簡易ハイライトで表す。
 */
const BASE_EMISSIVE = 0.12;
const HIGHLIGHT_EMISSIVE = 0.85;

/**
 * スナップ候補がある間の「薄く光る」状態（SPEC.md 5.1）。
 * 解釈: 選択ハイライト（同じ色を濃くする）と見分けられるよう、色を白へ寄せて霞んだ発光にする。
 */
const SNAP_HINT_EMISSIVE = 0.6;
const SNAP_HINT_WHITENESS = 0.75;
const SNAP_HINT_TINT = new THREE.Color(0xffffff);

/**
 * クリア演出で発光を最大まで上げたときの emissive 係数（SPEC.md 5.2-1）。
 * 内部コア（glowCores）と連動して、ピース本体そのものも明るくする。
 */
const CLEAR_EMISSIVE = 1.8;

/** 表示だけのずれ（ボクセル単位。補間中は整数にならない）。 */
export type ViewOffset = { readonly x: number; readonly y: number; readonly z: number };

/** ずれ無し。 */
const NO_OFFSET: ViewOffset = { x: 0, y: 0, z: 0 };

/** ピース群の描画。配置が変わったら updatePlacements を呼ぶ。 */
export type PieceViews = {
  /** シーンに追加するルート。N×N×N の中心が原点に来るようオフセットしてある。 */
  readonly object: THREE.Group;
  /** 全ピース色を混ぜた色。融合後の巨大クリスタルの色に使う（SPEC.md 5.2-2）。 */
  readonly blendedColor: THREE.Color;
  /** 配置を反映する。placements は全ピース分（順不同）。 */
  updatePlacements(placements: readonly Placement[]): void;
  /** アクティブなピースを光らせる（null で解除）。 */
  setHighlighted(pieceId: number | null): void;
  /** スナップ候補があるピースを薄く光らせる（null で解除）。選択ハイライトより優先する。 */
  setSnapHint(pieceId: number | null): void;
  /**
   * 表示だけを offset だけずらす（null でずれ無し）。
   * 論理上の配置は動かさないので、スナップの補間中でもクリア判定は整数座標のまま。
   */
  setOffset(pieceId: number, offset: ViewOffset | null): void;
  /**
   * 内部発光コアの明滅を進める（SPEC.md 4 章）。elapsedSeconds は起動からの経過秒。
   * ピースごとに位相をずらしたサイン波で呼吸する。
   */
  updateGlow(elapsedSeconds: number): void;
  /** クリア演出の発光ブースト（0 = 平常、1 = 最大）。コアとピース本体の両方に効く。 */
  setGlowBoost(amount: number): void;
  /** ピース群（本体 + コア）の表示。融合演出（SPEC.md 5.2-2）で false にする。 */
  setPiecesVisible(visible: boolean): void;
  /** ジオメトリ / マテリアルを解放する。 */
  dispose(): void;
};

/** ピースごとの色。色相を等間隔にずらして見分けられるようにする。 */
function pieceColor(index: number, count: number): THREE.Color {
  return new THREE.Color().setHSL((index / Math.max(count, 1) + 0.55) % 1, 0.62, 0.58);
}

/**
 * すりガラス（SPEC.md 4 章）。transmission を使うので transparent は立てない
 * （three は transmission > 0 のマテリアルを専用パスで描くため、透明扱いにすると描画順が乱れる）。
 *
 * M5 での追い込み: roughness は SPEC.md の 0.4〜0.6 の中で「すり」感が出る 0.52、ior は
 * ガラスの実測値 1.5、clearcoat を強めて表面のツヤを足し、attenuation でボクセルの厚みぶん
 * 色が乗るようにした。Fake 屈折（画面空間の歪み）は SPEC.md 4 章・10 章のとおり初版スコープ外。
 */
function createPieceMaterial(color: THREE.Color): THREE.MeshPhysicalMaterial {
  return new THREE.MeshPhysicalMaterial({
    color,
    roughness: 0.52,
    metalness: 0,
    transmission: 0.92,
    thickness: 1.1,
    ior: 1.5,
    // 厚みぶんだけピース色が濃く乗る。1 ボクセル分の距離を基準にする
    attenuationColor: color.clone(),
    attenuationDistance: 2.4,
    clearcoat: 0.7,
    clearcoatRoughness: 0.2,
    envMapIntensity: 1.5,
    emissive: color.clone().multiplyScalar(BASE_EMISSIVE),
  });
}

/**
 * 軽量モードのマテリアル（SPEC.md 4 章の負荷対策）。
 *
 * transmission は three が画面をもう一度レンダリングして屈折用のテクスチャを作るため、
 * 非力な端末では一気に重くなる。そこで半透明の MeshStandardMaterial に落とし、
 * 失われる「厚み感」は稜線の発光（edgeGlow）と内部コアで補う。
 * 切り替えは `?lite=1` / `?lite=0`、無指定なら端末判定（src/render/quality.ts）。
 */
function createLitePieceMaterial(color: THREE.Color): THREE.MeshStandardMaterial {
  return new THREE.MeshStandardMaterial({
    color,
    roughness: 0.34,
    metalness: 0,
    transparent: true,
    opacity: 0.66,
    // 半透明のボクセルが重なる順で絵が変わらないよう、深度は書かない
    depthWrite: false,
    envMapIntensity: 1.3,
    emissive: color.clone().multiplyScalar(BASE_EMISSIVE),
  });
}

/** createPieceViews の任意設定。 */
export type PieceViewsOptions = {
  /** 軽量モード（transmission を使わない）。既定は false。 */
  readonly lite?: boolean;
};

/**
 * ピース群の InstancedMesh を作る。ジオメトリは全ピースで 1 つを共有する。
 * n は解答空間のサイズで、立方体 [0, n-1]³ の中心が原点に来るようルートをずらすのに使う。
 */
export function createPieceViews(
  pieces: readonly Piece[],
  n: number,
  options: PieceViewsOptions = {},
): PieceViews {
  const lite = options.lite ?? false;
  const geometry = new THREE.BoxGeometry(VOXEL_SIZE, VOXEL_SIZE, VOXEL_SIZE);
  const object = new THREE.Group();
  object.position.setScalar(-(n - 1) / 2);

  const meshes = new Map<number, THREE.InstancedMesh>();
  const pieceById = new Map<number, Piece>();
  const materials: THREE.Material[] = [];
  // ハイライトの切り替えでピース色の emissive を作り直せるよう、色と材質を id で引けるようにする
  const materialById = new Map<number, THREE.MeshStandardMaterial>();
  const colorById = new Map<number, THREE.Color>();

  pieces.forEach((piece, index): void => {
    if (pieceById.has(piece.id)) throw new Error(`ピース id ${piece.id} が重複している`);
    pieceById.set(piece.id, piece);
    const color = pieceColor(index, pieces.length);
    const material = lite ? createLitePieceMaterial(color) : createPieceMaterial(color);
    materials.push(material);
    materialById.set(piece.id, material);
    colorById.set(piece.id, color);
    const mesh = new THREE.InstancedMesh(geometry, material, piece.voxels.length);
    mesh.name = `piece-${piece.id}`;
    mesh.frustumCulled = false;
    mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
    // 選択（M3）で InstancedMesh からピースを引けるようにしておく
    mesh.userData['pieceId'] = piece.id;
    meshes.set(piece.id, mesh);
    object.add(mesh);
  });

  // 内部発光コア（SPEC.md 4 章）。全ピース分をまとめて 1 セットにする
  const cores = createGlowCores(pieces, (pieceId): THREE.Color => {
    const color = colorById.get(pieceId);
    if (!color) throw new Error(`未知のピース id ${pieceId}`);
    return color;
  });
  object.add(cores.object);

  // 軽量モードだけ稜線を足す。全ピースまとめて 1 ドローコールなので追加コストはほぼ無い
  const edges: EdgeGlow | null = lite
    ? createEdgeGlow(
        pieces,
        (pieceId): THREE.Color => {
          const color = colorById.get(pieceId);
          if (!color) throw new Error(`未知のピース id ${pieceId}`);
          return color;
        },
        VOXEL_SIZE / 2,
      )
    : null;
  if (edges) object.add(edges.object);

  // 融合後の巨大クリスタルの色（SPEC.md 5.2-2）。全ピース色の平均。
  // 色相を等間隔に散らしてあるので単純平均だと灰色に寄る。彩度と明度は下限を入れて持ち上げる
  const blendedColor = new THREE.Color(0x8fd3ff);
  if (colorById.size > 0) {
    blendedColor.setRGB(0, 0, 0);
    for (const color of colorById.values()) blendedColor.add(color);
    blendedColor.multiplyScalar(1 / colorById.size);
    const hsl = { h: 0, s: 0, l: 0 };
    blendedColor.getHSL(hsl);
    blendedColor.setHSL(hsl.h, Math.max(hsl.s, 0.5), Math.max(hsl.l, 0.62));
  }

  let highlighted: number | null = null;
  let snapHinted: number | null = null;
  let glowBoost = 0;

  const matrix = new THREE.Matrix4();
  // 補間中の表示のずれと、そのやり直しに使う直近の配置
  const offsets = new Map<number, ViewOffset>();
  const lastPlacements = new Map<number, Placement>();

  /** 1 ピース分のインスタンス行列を書き直す。表示のずれはここでだけ足す。 */
  const applyPlacement = (placement: Placement): void => {
    const piece = pieceById.get(placement.pieceId);
    const mesh = meshes.get(placement.pieceId);
    if (!piece || !mesh) throw new Error(`未知のピース id ${placement.pieceId}`);
    const offset = offsets.get(placement.pieceId) ?? NO_OFFSET;
    // ボクセルは立方体なので向きは placedVoxels の座標に織り込み済み。行列は平行移動だけでよい
    placedVoxels(piece, placement).forEach((voxel, i): void => {
      const x = voxel.x + offset.x;
      const y = voxel.y + offset.y;
      const z = voxel.z + offset.z;
      matrix.makeTranslation(x, y, z);
      mesh.setMatrixAt(i, matrix);
      // 発光コアと稜線はボクセルの中心に置く（ボクセル本体と同じ座標）
      cores.setPosition(placement.pieceId, i, x, y, z);
      edges?.setPosition(placement.pieceId, i, x, y, z);
    });
    mesh.instanceMatrix.needsUpdate = true;
    // インスタンスを動かしたら境界球を捨てる。three の InstancedMesh.raycast は境界球との交差で
    // まずふるいに掛け、null のときだけ作り直す。古い球を使い回すと、動かしたピースが球の外へ
    // 出た瞬間にレイがまったく当たらなくなる（選択できないピースが出る原因）。
    // frustumCulled = false はカリングを止めるだけで、レイキャストの境界球には効かない。
    // 作り直しは動いた 1 ピース分だけで済む（updatePlacements が変化したピースにしか来ないため）
    mesh.boundingSphere = null;
    cores.flush();
    edges?.flush();
  };

  /**
   * 選択ハイライト・スナップ候補・クリア演出の発光を材質へ反映する。
   * クリア演出のブーストは他のどの状態よりも優先し、全ピースが連動して明るくなる。
   */
  const applyEmissive = (): void => {
    for (const [id, material] of materialById) {
      const color = colorById.get(id);
      if (!color) continue;
      if (id === snapHinted) {
        material.emissive
          .copy(color)
          .lerp(SNAP_HINT_TINT, SNAP_HINT_WHITENESS)
          .multiplyScalar(THREE.MathUtils.lerp(SNAP_HINT_EMISSIVE, CLEAR_EMISSIVE, glowBoost));
        continue;
      }
      const base = id === highlighted ? HIGHLIGHT_EMISSIVE : BASE_EMISSIVE;
      material.emissive
        .copy(color)
        .multiplyScalar(THREE.MathUtils.lerp(base, CLEAR_EMISSIVE, glowBoost));
    }
  };

  const updatePlacements = (placements: readonly Placement[]): void => {
    const seen = new Set<number>();
    for (const placement of placements) {
      if (seen.has(placement.pieceId)) {
        throw new Error(`ピース id ${placement.pieceId} の配置が重複している`);
      }
      seen.add(placement.pieceId);
      // 動いていないピースは行列を書き直さない。N=7 / M=40 でも 1 手あたりの更新が 1 ピース分で済む
      const previous = lastPlacements.get(placement.pieceId);
      const unchanged =
        previous !== undefined &&
        previous.orientation === placement.orientation &&
        equalsVec3(previous.position, placement.position);
      lastPlacements.set(placement.pieceId, placement);
      if (!unchanged) applyPlacement(placement);
    }
    if (seen.size !== pieceById.size) {
      throw new Error(`配置の数が足りない (ピース ${pieceById.size} / 配置 ${seen.size})`);
    }
  };

  return {
    object,
    blendedColor,
    updatePlacements,
    setHighlighted(pieceId: number | null): void {
      if (highlighted === pieceId) return;
      highlighted = pieceId;
      applyEmissive();
    },
    setSnapHint(pieceId: number | null): void {
      if (snapHinted === pieceId) return;
      snapHinted = pieceId;
      applyEmissive();
    },
    setOffset(pieceId: number, offset: ViewOffset | null): void {
      if (offset === null) {
        if (!offsets.delete(pieceId)) return;
      } else {
        offsets.set(pieceId, offset);
      }
      const placement = lastPlacements.get(pieceId);
      if (placement !== undefined) applyPlacement(placement);
    },
    updateGlow(elapsedSeconds: number): void {
      cores.update(elapsedSeconds);
    },
    setGlowBoost(amount: number): void {
      const next = THREE.MathUtils.clamp(amount, 0, 1);
      if (next === glowBoost) return;
      glowBoost = next;
      cores.setBoost(next);
      edges?.setBoost(next);
      applyEmissive();
    },
    setPiecesVisible(visible: boolean): void {
      object.visible = visible;
    },
    dispose(): void {
      offsets.clear();
      lastPlacements.clear();
      edges?.dispose();
      cores.dispose();
      for (const mesh of meshes.values()) mesh.dispose();
      for (const material of materials) material.dispose();
      geometry.dispose();
      object.clear();
    },
  };
}

/** 解答空間の枠。作り直しのたびに GPU 資源を解放できるよう dispose を持つ。 */
export type SolutionFrame = {
  readonly object: THREE.LineSegments;
  /** シーンから外し、ジオメトリ / マテリアルを解放する。 */
  dispose(): void;
};

/**
 * 解答空間 N×N×N の位置を示すワイヤーフレーム枠。
 * ピースがどこへ集まればよいかの目安として薄く出す（PieceViews と同じ原点合わせで使う）。
 */
export function createSolutionFrame(n: number): SolutionFrame {
  const box = new THREE.BoxGeometry(n, n, n);
  const edges = new THREE.EdgesGeometry(box);
  box.dispose();
  const material = new THREE.LineBasicMaterial({
    color: 0x5f7fbf,
    transparent: true,
    opacity: 0.35,
  });
  const object = new THREE.LineSegments(edges, material);
  return {
    object,
    dispose(): void {
      object.removeFromParent();
      edges.dispose();
      material.dispose();
    },
  };
}
