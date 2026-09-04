// クリア判定（SPEC.md 3.4）。物理判定は使わず、配列のインデックス計算だけで行う。
// Three.js に依存しない純粋なロジック。

import type { Vec3 } from './grid';
import { boundingBox, placedVoxels, type Piece, type Placement } from './piece';

/**
 * 全ピースが N×N×N の立方体にぴったり収まっているか。
 *
 * 変換後のボクセル総数が N³、重複が無く、外接ボックスの各辺がちょうど N ならクリア。
 * 立方体の位置（原点）は問わない。ピース群がどこにあってもよい。
 *
 * pieces と placements はピース id で対応付ける（並び順は問わない）。
 * id の重複・未知の id・配置漏れ / 二重配置は呼び出し側のバグなので Error を投げる。
 */
export function isSolved(
  pieces: readonly Piece[],
  placements: readonly Placement[],
  n: number,
): boolean {
  if (!Number.isInteger(n) || n < 1) {
    throw new RangeError(`空間サイズ N は 1 以上の整数 (got ${String(n)})`);
  }

  const byId = new Map<number, Piece>();
  for (const piece of pieces) {
    if (byId.has(piece.id)) throw new Error(`クリア判定: ピース id ${piece.id} が重複している`);
    byId.set(piece.id, piece);
  }
  if (placements.length !== byId.size) {
    throw new Error(
      `クリア判定: ピース数 ${byId.size} と配置数 ${placements.length} が一致しない`,
    );
  }

  // ボクセル総数が N³ でなければ、置き方によらずクリアにはなり得ない
  const total = n * n * n;
  let count = 0;
  for (const piece of pieces) count += piece.voxels.length;
  if (count !== total) return false;

  // 各配置をワールドのグリッド座標に変換して集める
  const world: Vec3[] = [];
  const placed = new Set<number>();
  for (const placement of placements) {
    const piece = byId.get(placement.pieceId);
    if (piece === undefined) {
      throw new Error(`クリア判定: 未知のピース id ${placement.pieceId}`);
    }
    if (placed.has(placement.pieceId)) {
      throw new Error(`クリア判定: ピース id ${placement.pieceId} の配置が重複している`);
    }
    placed.add(placement.pieceId);
    for (const v of placedVoxels(piece, placement)) world.push(v);
  }

  // 外接ボックスが N×N×N でなければ、はみ出しか隙間がある
  const box = boundingBox(world);
  if (box.size.x !== n || box.size.y !== n || box.size.z !== n) return false;

  // ここまで来れば全ボクセルは箱の中。重複が 1 つも無ければ N³ マスをちょうど埋めている
  const seen: boolean[] = new Array<boolean>(total).fill(false);
  for (const v of world) {
    const index = ((v.x - box.min.x) * n + (v.y - box.min.y)) * n + (v.z - box.min.z);
    if (seen[index] === true) return false;
    seen[index] = true;
  }
  return true;
}
