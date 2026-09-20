// 縁の発光（軽量モード専用）。ボクセルの稜線を光らせてクリスタルの輪郭を出す。
//
// 軽量モードでは MeshPhysicalMaterial の transmission（＝すりガラス）を諦めて半透明の
// MeshStandardMaterial にするため、そのままだと形が読み取りにくい。そこで稜線を足す。
// ただし **ドローコールを増やさない**（SPEC.md 4 章 / CLAUDE.md 開発ルール 4）のが要なので、
// ピースごとに LineSegments を作らず、**全ピースの全ボクセルの稜線を 1 本の LineSegments** に
// まとめる。位置は PieceViews が配置更新のたびに setPosition で書き込む（GlowCores と同じ形）。

import * as THREE from 'three';
import type { Piece } from '../core/piece';

/** 立方体の 8 頂点（単位は「半辺」）。 */
const CORNERS: readonly (readonly [number, number, number])[] = [
  [-1, -1, -1],
  [1, -1, -1],
  [1, 1, -1],
  [-1, 1, -1],
  [-1, -1, 1],
  [1, -1, 1],
  [1, 1, 1],
  [-1, 1, 1],
];

/** 立方体の 12 稜線（CORNERS の添字の組）。 */
const EDGES: readonly (readonly [number, number])[] = [
  [0, 1],
  [1, 2],
  [2, 3],
  [3, 0],
  [4, 5],
  [5, 6],
  [6, 7],
  [7, 4],
  [0, 4],
  [1, 5],
  [2, 6],
  [3, 7],
];

/** ボクセル 1 個ぶんの頂点数（12 稜線 × 2 端点）。 */
const VERTICES_PER_VOXEL = EDGES.length * 2;

/** 稜線の色をピース色から作るときの白寄せ量。 */
const EDGE_WHITENESS = 0.35;

/** クリア演出で最大まで上げたときの明るさ倍率。 */
const BURST_INTENSITY = 2.6;

export type EdgeGlow = {
  /** シーン（ピースのルート）に追加する LineSegments。1 本で全ピース分。 */
  readonly object: THREE.LineSegments;
  /** ピース pieceId の index 番目のボクセル中心を書き込む。書き終えたら flush() を呼ぶ。 */
  setPosition(pieceId: number, index: number, x: number, y: number, z: number): void;
  /** setPosition の結果を GPU へ反映する。 */
  flush(): void;
  /** クリア演出の発光ブースト（0 = 平常、1 = 最大）。 */
  setBoost(amount: number): void;
  /** ジオメトリ / マテリアルを解放する。 */
  dispose(): void;
};

/** ピース 1 つ分の頂点区間。 */
type Range = { readonly start: number; readonly count: number };

/**
 * 全ピースのボクセル稜線を 1 本にまとめた LineSegments を作る。
 * halfSize はボクセルの半辺（PieceViews の VOXEL_SIZE / 2 を渡す想定）。
 */
export function createEdgeGlow(
  pieces: readonly Piece[],
  colorOf: (pieceId: number) => THREE.Color,
  halfSize: number,
): EdgeGlow {
  const totalVoxels = pieces.reduce((sum, piece): number => sum + piece.voxels.length, 0);
  const vertexCount = Math.max(totalVoxels, 1) * VERTICES_PER_VOXEL;
  const positions = new Float32Array(vertexCount * 3);
  const colors = new Float32Array(vertexCount * 3);

  // ボクセル中心からの稜線オフセット。setPosition ではこれを足すだけにする
  const offsets = new Float32Array(VERTICES_PER_VOXEL * 3);
  EDGES.forEach(([from, to], edge): void => {
    const a = CORNERS[from];
    const b = CORNERS[to];
    if (!a || !b) throw new Error('稜線の頂点が見つからない');
    const base = edge * 6;
    offsets[base] = a[0] * halfSize;
    offsets[base + 1] = a[1] * halfSize;
    offsets[base + 2] = a[2] * halfSize;
    offsets[base + 3] = b[0] * halfSize;
    offsets[base + 4] = b[1] * halfSize;
    offsets[base + 5] = b[2] * halfSize;
  });

  const ranges = new Map<number, Range>();
  let cursor = 0;
  const tint = new THREE.Color();
  for (const piece of pieces) {
    const count = piece.voxels.length * VERTICES_PER_VOXEL;
    ranges.set(piece.id, { start: cursor, count });
    tint.copy(colorOf(piece.id)).lerp(new THREE.Color(0xffffff), EDGE_WHITENESS);
    for (let i = cursor; i < cursor + count; i += 1) {
      colors[i * 3] = tint.r;
      colors[i * 3 + 1] = tint.g;
      colors[i * 3 + 2] = tint.b;
    }
    cursor += count;
  }

  const geometry = new THREE.BufferGeometry();
  const positionAttribute = new THREE.BufferAttribute(positions, 3);
  positionAttribute.setUsage(THREE.DynamicDrawUsage);
  geometry.setAttribute('position', positionAttribute);
  geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));

  const material = new THREE.LineBasicMaterial({
    vertexColors: true,
    transparent: true,
    opacity: 0.85,
    depthWrite: false,
    blending: THREE.AdditiveBlending,
  });

  const object = new THREE.LineSegments(geometry, material);
  object.name = 'edge-glow';
  // 位置が入るまで境界球が定まらないので、カリングは切って毎フレームの再計算も避ける
  object.frustumCulled = false;
  object.renderOrder = 1;
  // ピース選択のレイキャスト（src/input）には拾わせない
  object.raycast = (): void => {};

  return {
    object,

    setPosition(pieceId, index, x, y, z): void {
      const range = ranges.get(pieceId);
      if (!range) return;
      const start = range.start + index * VERTICES_PER_VOXEL;
      if (index < 0 || start + VERTICES_PER_VOXEL > range.start + range.count) return;
      for (let v = 0; v < VERTICES_PER_VOXEL; v += 1) {
        const dst = (start + v) * 3;
        const src = v * 3;
        positions[dst] = x + (offsets[src] ?? 0);
        positions[dst + 1] = y + (offsets[src + 1] ?? 0);
        positions[dst + 2] = z + (offsets[src + 2] ?? 0);
      }
    },

    flush(): void {
      positionAttribute.needsUpdate = true;
    },

    setBoost(amount: number): void {
      // LineBasicMaterial.color は vertexColors に掛かるので、1 行で全体の明るさを上げられる
      const level = THREE.MathUtils.lerp(1, BURST_INTENSITY, THREE.MathUtils.clamp(amount, 0, 1));
      material.color.setScalar(level);
    },

    dispose(): void {
      object.removeFromParent();
      geometry.dispose();
      material.dispose();
    },
  };
}
