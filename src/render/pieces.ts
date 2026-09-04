// ピース群の描画。1 ピース = 1 InstancedMesh（CLAUDE.md 開発ルール 4 / SPEC.md 4 章）。
// 位置・向きの解釈は core に任せ、ここは placedVoxels の結果を行列に落とすだけにする。

import * as THREE from 'three';
import { placedVoxels, type Piece, type Placement } from '../core/piece';

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

/** ピース群の描画。配置が変わったら updatePlacements を呼ぶ。 */
export type PieceViews = {
  /** シーンに追加するルート。N×N×N の中心が原点に来るようオフセットしてある。 */
  readonly object: THREE.Group;
  /** ピース id → その InstancedMesh。選択ハイライト（M3）から引けるように公開する。 */
  readonly meshes: ReadonlyMap<number, THREE.InstancedMesh>;
  /** 配置を反映する。placements は全ピース分（順不同）。 */
  updatePlacements(placements: readonly Placement[]): void;
  /** アクティブなピースを光らせる（null で解除）。 */
  setHighlighted(pieceId: number | null): void;
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
 * 質感の追い込みは M5 で行う。ここは形が見えれば十分。
 */
function createPieceMaterial(color: THREE.Color): THREE.MeshPhysicalMaterial {
  return new THREE.MeshPhysicalMaterial({
    color,
    roughness: 0.45,
    metalness: 0,
    transmission: 0.85,
    thickness: 0.9,
    ior: 1.45,
    clearcoat: 0.35,
    clearcoatRoughness: 0.35,
    envMapIntensity: 1.2,
    emissive: color.clone().multiplyScalar(BASE_EMISSIVE),
  });
}

/**
 * ピース群の InstancedMesh を作る。ジオメトリは全ピースで 1 つを共有する。
 * n は解答空間のサイズで、立方体 [0, n-1]³ の中心が原点に来るようルートをずらすのに使う。
 */
export function createPieceViews(pieces: readonly Piece[], n: number): PieceViews {
  const geometry = new THREE.BoxGeometry(VOXEL_SIZE, VOXEL_SIZE, VOXEL_SIZE);
  const object = new THREE.Group();
  object.position.setScalar(-(n - 1) / 2);

  const meshes = new Map<number, THREE.InstancedMesh>();
  const pieceById = new Map<number, Piece>();
  const materials: THREE.MeshPhysicalMaterial[] = [];
  // ハイライトの切り替えでピース色の emissive を作り直せるよう、色と材質を id で引けるようにする
  const materialById = new Map<number, THREE.MeshPhysicalMaterial>();
  const colorById = new Map<number, THREE.Color>();

  pieces.forEach((piece, index): void => {
    if (pieceById.has(piece.id)) throw new Error(`ピース id ${piece.id} が重複している`);
    pieceById.set(piece.id, piece);
    const color = pieceColor(index, pieces.length);
    const material = createPieceMaterial(color);
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

  let highlighted: number | null = null;

  const matrix = new THREE.Matrix4();

  const updatePlacements = (placements: readonly Placement[]): void => {
    const seen = new Set<number>();
    for (const placement of placements) {
      const piece = pieceById.get(placement.pieceId);
      const mesh = meshes.get(placement.pieceId);
      if (!piece || !mesh) throw new Error(`未知のピース id ${placement.pieceId}`);
      if (seen.has(placement.pieceId)) {
        throw new Error(`ピース id ${placement.pieceId} の配置が重複している`);
      }
      seen.add(placement.pieceId);
      // ボクセルは立方体なので向きは placedVoxels の座標に織り込み済み。行列は平行移動だけでよい
      placedVoxels(piece, placement).forEach((voxel, i): void => {
        matrix.makeTranslation(voxel.x, voxel.y, voxel.z);
        mesh.setMatrixAt(i, matrix);
      });
      mesh.instanceMatrix.needsUpdate = true;
      mesh.computeBoundingSphere();
    }
    if (seen.size !== pieceById.size) {
      throw new Error(`配置の数が足りない (ピース ${pieceById.size} / 配置 ${seen.size})`);
    }
  };

  return {
    object,
    meshes,
    updatePlacements,
    setHighlighted(pieceId: number | null): void {
      if (highlighted === pieceId) return;
      highlighted = pieceId;
      for (const [id, material] of materialById) {
        const color = colorById.get(id);
        if (!color) continue;
        material.emissive
          .copy(color)
          .multiplyScalar(id === pieceId ? HIGHLIGHT_EMISSIVE : BASE_EMISSIVE);
      }
    },
    dispose(): void {
      for (const mesh of meshes.values()) mesh.dispose();
      for (const material of materials) material.dispose();
      geometry.dispose();
      object.clear();
    },
  };
}

/**
 * 解答空間 N×N×N の位置を示すワイヤーフレーム枠。
 * ピースがどこへ集まればよいかの目安として薄く出す（PieceViews と同じ原点合わせで使う）。
 */
export function createSolutionFrame(n: number): THREE.LineSegments {
  const box = new THREE.BoxGeometry(n, n, n);
  const edges = new THREE.EdgesGeometry(box);
  box.dispose();
  return new THREE.LineSegments(
    edges,
    new THREE.LineBasicMaterial({ color: 0x5f7fbf, transparent: true, opacity: 0.35 }),
  );
}
