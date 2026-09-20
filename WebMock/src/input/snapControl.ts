// 手を離したときのマグネットスナップ（SPEC.md 3.5 / 5.1）。
// 候補を求めるのは core の純粋関数 snapCandidate。ここは「いつ呼ぶか」と結果の配り先だけを持つ。

import { equalsVec3 } from '../core/grid';
import type { Piece, Placement } from '../core/piece';
import { snapCandidate } from '../core/solve';

export type SnapControlOptions = {
  readonly pieces: readonly Piece[];
  /** 解答空間のサイズ N。 */
  readonly n: number;
  /** 現在の全ピースの配置。 */
  readonly placements: () => readonly Placement[];
  /** 吸い付く先があるピースが変わったとき（無くなったら null）。薄い発光の切り替えに使う。 */
  readonly onHintChange: (pieceId: number | null) => void;
  /** 実際に吸い付いたとき。from は吸着前の配置、to は吸着後の配置。 */
  readonly onSnap: (from: Placement, to: Placement) => void;
};

export type SnapControl = {
  /**
   * アクティブなピースの候補を計算し直してヒントを更新する。
   * 配置が変わったタイミング（ボクセル 1 マスの移動・回転・選択の切り替え）で呼ぶ。
   * 毎フレーム呼ぶ必要は無い。
   */
  refresh(pieceId: number | null): void;
  /** 手を離したときに呼ぶ。吸い付く先があれば onSnap を出して true。 */
  release(pieceId: number): boolean;
};

/** スナップの制御。ゲーム状態は placements() 越しに読むだけで、更新は onSnap に任せる。 */
export function createSnapControl(options: SnapControlOptions): SnapControl {
  let hinted: number | null = null;

  const setHint = (pieceId: number | null): void => {
    if (hinted === pieceId) return;
    hinted = pieceId;
    options.onHintChange(pieceId);
  };

  /**
   * 吸い付く先を求める。
   * 現在位置がそのまま候補になる（＝すでに収まっている）ときは「吸い付く先」ではないので捨てる。
   */
  const compute = (pieceId: number | null): Placement | null => {
    if (pieceId === null) return null;
    const placements = options.placements();
    const active = placements.find((placement): boolean => placement.pieceId === pieceId);
    if (active === undefined) return null;
    const candidate = snapCandidate(options.pieces, placements, pieceId, options.n);
    if (candidate === null || equalsVec3(candidate.position, active.position)) return null;
    return candidate;
  };

  return {
    refresh(pieceId): void {
      setHint(compute(pieceId) === null ? null : pieceId);
    },

    release(pieceId): boolean {
      // 離した時点の配置で計算し直す（refresh 後にドラッグが進んでいることがある）
      const candidate = compute(pieceId);
      const from = options
        .placements()
        .find((placement): boolean => placement.pieceId === pieceId);
      setHint(null);
      if (candidate === null || from === undefined) return false;
      options.onSnap(from, candidate);
      return true;
    },
  };
}
