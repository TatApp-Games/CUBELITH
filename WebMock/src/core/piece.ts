// ピースの表現と、配置（向き + 位置）からワールド座標への変換（SPEC.md 3.2 / 3.4）。
// Three.js に依存しない純粋なロジック。

import { addVec3, compareVec3, rotateVoxel, subVec3, vec3, type Vec3 } from './grid';

/** ピース。voxels は局所座標のボクセル集合で、正規化後は局所原点が (0,0,0) になる。 */
export type Piece = { readonly id: number; readonly voxels: readonly Vec3[] };

/** 配置。ピースを orientation で回してから position へ平行移動する。 */
export type Placement = {
  readonly pieceId: number;
  readonly orientation: number;
  readonly position: Vec3;
};

/** ボクセル集合の外接ボックス。size は含まれるマス数（max - min + 1）。 */
export type BoundingBox = { readonly min: Vec3; readonly max: Vec3; readonly size: Vec3 };

// 重心との距離を比べるときの許容誤差。重心は有理数なので厳密な同点が浮動小数で僅かにずれる。
const DISTANCE_EPSILON = 1e-9;

/**
 * 局所原点にするボクセルを返す。
 * 解釈: SPEC.md 3.3 は「最初のボクセル、または重心に最も近いボクセル」と選択肢を示しているが、
 * 回転しても見た目の中心がずれにくい後者を採る。同点のときは座標の辞書順で最小のものを選び決定的にする。
 */
export function localOrigin(voxels: readonly Vec3[]): Vec3 {
  const first = voxels[0];
  if (first === undefined) throw new RangeError('localOrigin: ボクセル集合が空');

  let sumX = 0;
  let sumY = 0;
  let sumZ = 0;
  for (const v of voxels) {
    sumX += v.x;
    sumY += v.y;
    sumZ += v.z;
  }
  const centerX = sumX / voxels.length;
  const centerY = sumY / voxels.length;
  const centerZ = sumZ / voxels.length;

  const squaredDistance = (v: Vec3): number => {
    const dx = v.x - centerX;
    const dy = v.y - centerY;
    const dz = v.z - centerZ;
    return dx * dx + dy * dy + dz * dz;
  };

  let best = first;
  let bestDistance = squaredDistance(first);
  for (const v of voxels) {
    const distance = squaredDistance(v);
    if (distance < bestDistance - DISTANCE_EPSILON) {
      best = v;
      bestDistance = distance;
    } else if (distance <= bestDistance + DISTANCE_EPSILON && compareVec3(v, best) < 0) {
      best = v;
      bestDistance = Math.min(bestDistance, distance);
    }
  }
  return vec3(best.x, best.y, best.z);
}

/** 局所原点が (0,0,0) になるよう平行移動したピースを返す。ボクセルの並びは変えない。 */
export function normalizePiece(piece: Piece): Piece {
  const origin = localOrigin(piece.voxels);
  return { id: piece.id, voxels: piece.voxels.map((v) => subVec3(v, origin)) };
}

/** id とボクセル集合から正規化済みのピースを作る。 */
export function createPiece(id: number, voxels: readonly Vec3[]): Piece {
  return normalizePiece({ id, voxels });
}

/** 配置をワールドのボクセル座標に変換する。向きを適用してから position を加算する。 */
export function placedVoxels(piece: Piece, placement: Placement): Vec3[] {
  if (piece.id !== placement.pieceId) {
    throw new Error(
      `placedVoxels: ピース id が一致しない (piece ${piece.id} / placement ${placement.pieceId})`,
    );
  }
  return piece.voxels.map((v) => addVec3(rotateVoxel(v, placement.orientation), placement.position));
}

/** ボクセル集合の外接ボックス。クリア判定（SPEC.md 3.4）で使う。 */
export function boundingBox(voxels: readonly Vec3[]): BoundingBox {
  const first = voxels[0];
  if (first === undefined) throw new RangeError('boundingBox: ボクセル集合が空');

  let minX = first.x;
  let minY = first.y;
  let minZ = first.z;
  let maxX = first.x;
  let maxY = first.y;
  let maxZ = first.z;
  for (const v of voxels) {
    if (v.x < minX) minX = v.x;
    if (v.y < minY) minY = v.y;
    if (v.z < minZ) minZ = v.z;
    if (v.x > maxX) maxX = v.x;
    if (v.y > maxY) maxY = v.y;
    if (v.z > maxZ) maxZ = v.z;
  }
  return {
    min: vec3(minX, minY, minZ),
    max: vec3(maxX, maxY, maxZ),
    size: vec3(maxX - minX + 1, maxY - minY + 1, maxZ - minZ + 1),
  };
}
