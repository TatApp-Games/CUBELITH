// 残りピース数（SPEC.md 6 章の「未確定のピース数」）の数え上げ。
//
// 解釈: SPEC.md は「未確定」の定義を書いておらず、ピースに確定という状態も無い。ここでは
// 「解答が成立する立方体の一部として整合しているピース」を確定とみなし、次の手順で数える。
//   1. 面で接しているピース同士を連結成分（塊）にまとめる
//   2. 最大の塊を「組み上がりつつある本体」とする。ただしピース 1 個だけの塊は本体としない
//      （散らした直後に 1 個だけ確定扱いになるのを避けるため）
//   3. 本体の外接ボックスが N×N×N に収まっていれば、本体のピースを確定とみなす。
//      収まっていなければ立方体になり得ないので、その塊は確定ではない
// クリアした瞬間は全ピースが 1 つの塊になり外接ボックスが N×N×N なので残りは 0 になる。
//
// この計算自体は Three.js に依存しない純粋関数だが、このタスクの scope に src/core と tests が
// 無いので src/ui 側に置いている。core へ移してテストを足すのは後続タスクで行う。

import { vec3, type Vec3 } from '../core/grid';
import { boundingBox, placedVoxels, type Piece, type Placement } from '../core/piece';

/** 6 近傍。「少なくとも 1 面で接する」の判定に使う。 */
const FACE_NEIGHBORS: readonly Vec3[] = [
  vec3(1, 0, 0),
  vec3(-1, 0, 0),
  vec3(0, 1, 0),
  vec3(0, -1, 0),
  vec3(0, 0, 1),
  vec3(0, 0, -1),
];

/** ボクセル座標の Map キー。 */
function voxelKey(x: number, y: number, z: number): string {
  return `${x},${y},${z}`;
}

/**
 * まだ確定していないピースの数を返す（0 なら全ピースが本体に収まっている）。
 * pieces に無い id の配置は無視する。
 */
export function unsettledPieceCount(
  pieces: readonly Piece[],
  placements: readonly Placement[],
  n: number,
): number {
  const pieceById = new Map(pieces.map((piece): [number, Piece] => [piece.id, piece]));

  const ids: number[] = [];
  const voxelsById = new Map<number, readonly Vec3[]>();
  const ownerByVoxel = new Map<string, number>();
  for (const placement of placements) {
    const piece = pieceById.get(placement.pieceId);
    if (piece === undefined) continue;
    const voxels = placedVoxels(piece, placement);
    ids.push(placement.pieceId);
    voxelsById.set(placement.pieceId, voxels);
    for (const voxel of voxels) {
      const key = voxelKey(voxel.x, voxel.y, voxel.z);
      // プレイ中はピースが重なり得る。その場合は先に置いたピースをそのマスの代表にする
      if (!ownerByVoxel.has(key)) ownerByVoxel.set(key, placement.pieceId);
    }
  }
  if (ids.length === 0) return 0;

  // 面接触のグラフを作る
  const neighbors = new Map<number, Set<number>>();
  for (const id of ids) neighbors.set(id, new Set<number>());
  for (const id of ids) {
    for (const voxel of voxelsById.get(id) ?? []) {
      for (const offset of FACE_NEIGHBORS) {
        const other = ownerByVoxel.get(
          voxelKey(voxel.x + offset.x, voxel.y + offset.y, voxel.z + offset.z),
        );
        if (other === undefined || other === id) continue;
        neighbors.get(id)?.add(other);
        neighbors.get(other)?.add(id);
      }
    }
  }

  // 連結成分を幅優先で列挙し、最大のものを本体とする（component 自身をキューに使う）
  const visited = new Set<number>();
  let largest: readonly number[] = [];
  for (const id of ids) {
    if (visited.has(id)) continue;
    visited.add(id);
    const component: number[] = [id];
    for (let head = 0; head < component.length; head += 1) {
      const current = component[head];
      if (current === undefined) continue;
      for (const next of neighbors.get(current) ?? []) {
        if (visited.has(next)) continue;
        visited.add(next);
        component.push(next);
      }
    }
    if (component.length > largest.length) largest = component;
  }

  if (largest.length < 2) return ids.length;

  const bodyVoxels: Vec3[] = [];
  for (const id of largest) {
    for (const voxel of voxelsById.get(id) ?? []) bodyVoxels.push(voxel);
  }
  const box = boundingBox(bodyVoxels);
  if (box.size.x > n || box.size.y > n || box.size.z > n) return ids.length;
  return ids.length - largest.length;
}
