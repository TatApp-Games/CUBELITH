import { describe, expect, it } from 'vitest';
import { vec3 } from '../src/core/grid';
import type { Placement } from '../src/core/piece';
import { generatePuzzle, scatterPlacements } from '../src/core/generate';
import { pickHintPiece } from '../src/core/hint';

/** id 0..count-1 の解答配置。位置と向きは id ごとに変える。 */
function solutionOf(count: number): Placement[] {
  return Array.from(
    { length: count },
    (_, id): Placement => ({ pieceId: id, orientation: id % 24, position: vec3(id, 0, 0) }),
  );
}

/** 解答から 1 マスずらした配置（位置違い）。 */
function movedFrom(placement: Placement): Placement {
  return {
    pieceId: placement.pieceId,
    orientation: placement.orientation,
    position: vec3(placement.position.x, placement.position.y + 5, placement.position.z),
  };
}

/** 解答と向きだけ違う配置。 */
function turnedFrom(placement: Placement): Placement {
  return {
    pieceId: placement.pieceId,
    orientation: (placement.orientation + 1) % 24,
    position: placement.position,
  };
}

/** 配列から id で配置を引く。 */
function pick(placements: readonly Placement[], pieceId: number): Placement {
  const found = placements.find((p): boolean => p.pieceId === pieceId);
  if (found === undefined) throw new Error(`テスト: ピース ${pieceId} の配置が無い`);
  return found;
}

describe('pickHintPiece', () => {
  it('未固定が 0 個なら null', () => {
    const solution = solutionOf(4);
    const placements = solution.map(movedFrom);
    expect(pickHintPiece(placements, solution, [0, 1, 2, 3])).toBeNull();
  });

  it('未固定が 1 個なら null（最後の 1 ピースはヒントを使えない）', () => {
    const solution = solutionOf(4);
    const placements = solution.map(movedFrom);
    expect(pickHintPiece(placements, solution, [0, 1, 2])).toBeNull();
    expect(pickHintPiece(placements, solution, new Set([1, 2, 3]))).toBeNull();
  });

  it('未固定が 2 個以上なら、解答とずれているうち id 最小を返す', () => {
    const solution = solutionOf(5);
    const placements = solution.map(movedFrom);
    expect(pickHintPiece(placements, solution, [])).toBe(0);
    expect(pickHintPiece(placements, solution, [0])).toBe(1);
    expect(pickHintPiece(placements, solution, [0, 2])).toBe(1);
    expect(pickHintPiece(placements, solution, [0, 1])).toBe(2);
  });

  it('解答と一致しているピースは id が小さくても選ばれない', () => {
    const solution = solutionOf(5);
    // 0 と 1 は解答どおり、2 以降はずれている
    const placements = solution.map((p): Placement => (p.pieceId <= 1 ? p : movedFrom(p)));
    expect(pickHintPiece(placements, solution, [])).toBe(2);
  });

  it('向きだけ違うピースも「ずれている」と見なす', () => {
    const solution = solutionOf(4);
    const placements = solution.map((p): Placement => (p.pieceId === 2 ? turnedFrom(p) : p));
    expect(pickHintPiece(placements, solution, [])).toBe(2);
  });

  it('未固定がすべて解答と一致していれば、未固定の id 最小を返す', () => {
    const solution = solutionOf(4);
    const placements = solution.slice();
    expect(pickHintPiece(placements, solution, [])).toBe(0);
    expect(pickHintPiece(placements, solution, [0])).toBe(1);
  });

  it('placements の並びが変わっても結果は同じ', () => {
    const solution = solutionOf(5);
    const placements = solution.map((p): Placement => (p.pieceId <= 1 ? p : movedFrom(p)));
    const shuffled = [...placements].reverse();
    expect(pickHintPiece(shuffled, solution, [])).toBe(2);
  });

  it('同じ入力なら常に同じ結果', () => {
    const solution = solutionOf(6);
    const placements = solution.map((p): Placement => (p.pieceId % 2 === 0 ? p : movedFrom(p)));
    const locked = [0, 1];
    const first = pickHintPiece(placements, solution, locked);
    for (let i = 0; i < 5; i++) {
      expect(pickHintPiece(placements, solution, locked)).toBe(first);
    }
    expect(first).toBe(3);
  });

  it('固定を進めていくと最後は null になる', () => {
    const solution = solutionOf(4);
    const placements = solution.map(movedFrom);
    const locked: number[] = [];
    const picked: number[] = [];
    for (;;) {
      const next = pickHintPiece(placements, solution, locked);
      if (next === null) break;
      picked.push(next);
      locked.push(next);
    }
    // 4 ピースなら 3 回まで（最後の 1 ピースは残る）
    expect(picked).toEqual([0, 1, 2]);
  });

  it('solution に無い id が placements にあれば Error', () => {
    const solution = solutionOf(3);
    const placements = [
      ...solution,
      { pieceId: 9, orientation: 0, position: vec3(0, 0, 0) } satisfies Placement,
    ];
    expect(() => pickHintPiece(placements, solution, [])).toThrow();
  });

  it('生成した実際のパズルでも動く', () => {
    const generated = generatePuzzle(4, 5, 20260906);
    const scattered = scatterPlacements(generated.pieces, 4, 20260906);
    // 散らした直後はすべてずれている想定だが、たまたま一致していても id 最小が返るのは同じ
    const first = pickHintPiece(scattered, generated.solution, []);
    expect(first).toBe(0);

    // ヒントを適用した状態（解答位置に置いて固定）にしても、次は未固定の中から選ばれる
    const applied = scattered.map((p): Placement => (p.pieceId === 0 ? pick(generated.solution, 0) : p));
    expect(pickHintPiece(applied, generated.solution, [0])).toBe(1);
  });
});
