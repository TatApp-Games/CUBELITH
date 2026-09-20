// ヒント対象のピース選定。Three.js に依存しない純粋なロジック。
// ヒントは「選ばれたピースを解答の位置・向きへ置いて固定する」機能で、ここはその対象選びだけを担う。

import { equalsVec3 } from './grid';
import type { Placement } from './piece';

/** 配置が解答と完全に一致しているか（位置と向きの両方）。 */
function matchesSolution(placement: Placement, solution: Placement): boolean {
  return (
    placement.orientation === solution.orientation &&
    equalsVec3(placement.position, solution.position)
  );
}

/**
 * ヒントで正解位置へ送るピースの id を選ぶ。使えるヒントが無ければ null。
 *
 * - 未固定（lockedIds に含まれない）のピースが 1 個以下なら null。
 *   最後の 1 ピースをヒントで埋めると操作せずクリアできてしまうため（要望の制約）。
 * - 未固定のうち、現在の配置が解答と異なるものの中で id が最小のものを選ぶ。
 * - 未固定がすべて解答と一致していれば、未固定の id が最小のものを選ぶ。
 *
 * 解釈: 選び方は要望に指定が無いので、上のように決定的な規則にする（同じ盤面では常に同じヒント）。
 */
export function pickHintPiece(
  placements: readonly Placement[],
  solution: readonly Placement[],
  lockedIds: Iterable<number>,
): number | null {
  const solutionById = new Map<number, Placement>();
  for (const placement of solution) solutionById.set(placement.pieceId, placement);

  const locked = new Set<number>(lockedIds);
  let fallback: number | null = null;
  let target: number | null = null;
  let unlockedCount = 0;

  for (const placement of placements) {
    const answer = solutionById.get(placement.pieceId);
    if (answer === undefined) {
      throw new Error(`ヒント: 解答に無いピース id ${placement.pieceId}`);
    }
    if (locked.has(placement.pieceId)) continue;
    unlockedCount++;
    if (fallback === null || placement.pieceId < fallback) fallback = placement.pieceId;
    if (matchesSolution(placement, answer)) continue;
    if (target === null || placement.pieceId < target) target = placement.pieceId;
  }

  // 最後の 1 ピースはヒントを使えない（未固定 0 個 / 1 個は null）
  if (unlockedCount <= 1) return null;
  return target ?? fallback;
}
