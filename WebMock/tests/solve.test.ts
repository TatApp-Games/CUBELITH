import { describe, expect, it } from 'vitest';
import {
  addVec3,
  IDENTITY_ORIENTATION,
  ORIENTATION_COUNT,
  vec3,
  vec3Key,
  type Vec3,
} from '../src/core/grid';
import { createPiece, placedVoxels, type Piece, type Placement } from '../src/core/piece';
import { generatePuzzle, maxPieces, MIN_PIECE_COUNT } from '../src/core/generate';
import { isSolved } from '../src/core/solve';

const SIZES: readonly number[] = [3, 4, 5, 6, 7];
const SEEDS: readonly number[] = [1, 7, 12345];

/** 6 近傍のオフセット（1 マスずらす方向）。 */
const STEPS: readonly Vec3[] = [
  vec3(1, 0, 0),
  vec3(-1, 0, 0),
  vec3(0, 1, 0),
  vec3(0, -1, 0),
  vec3(0, 0, 1),
  vec3(0, 0, -1),
];

/** M の代表値（下限・中間・上限）。 */
function representativePieceCounts(n: number): number[] {
  const limit = maxPieces(n);
  const middle = Math.floor((MIN_PIECE_COUNT + limit) / 2);
  return [...new Set([MIN_PIECE_COUNT, middle, limit])];
}

/** 配置の 1 つだけを差し替えた配列を返す。 */
function replaceAt(
  placements: readonly Placement[],
  index: number,
  placement: Placement,
): Placement[] {
  const copy = [...placements];
  copy[index] = placement;
  return copy;
}

/** 全体を平行移動した配置。 */
function translateAll(placements: readonly Placement[], shift: Vec3): Placement[] {
  return placements.map(
    (p): Placement => ({ ...p, position: addVec3(p.position, shift) }),
  );
}

/** ボクセル集合を比較用のキー集合にする。 */
function keySet(voxels: readonly Vec3[]): Set<string> {
  return new Set(voxels.map(vec3Key));
}

describe('isSolved（手作りの小さな例）', () => {
  // 2x2x2 を 2 枚のスラブ（1x2x2）に分けたもの
  const slabVoxels: readonly Vec3[] = [vec3(0, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 1, 1)];
  const slabs: readonly Piece[] = [createPiece(0, slabVoxels), createPiece(1, slabVoxels)];
  /** 局所原点が (0,0,0) に来るよう正規化されているので、置きたい絶対座標から原点分を引く */
  const slabPlacement = (id: number, x: number): Placement => ({
    pieceId: id,
    orientation: IDENTITY_ORIENTATION,
    position: vec3(x, 0, 0),
  });

  it('2 枚のスラブが並ぶとクリア', () => {
    expect(isSolved(slabs, [slabPlacement(0, 0), slabPlacement(1, 1)], 2)).toBe(true);
  });

  it('原点は問わない（どこにあってもクリア）', () => {
    const shifted = [
      { ...slabPlacement(0, 0), position: vec3(-5, 3, -9) },
      { ...slabPlacement(1, 0), position: vec3(-4, 3, -9) },
    ];
    expect(isSolved(slabs, shifted, 2)).toBe(true);
  });

  it('離れているとクリアにならない', () => {
    expect(isSolved(slabs, [slabPlacement(0, 0), slabPlacement(1, 2)], 2)).toBe(false);
  });

  it('重なっているとクリアにならない', () => {
    expect(isSolved(slabs, [slabPlacement(0, 0), slabPlacement(1, 0)], 2)).toBe(false);
  });

  it('ボクセル総数が N³ でなければクリアにならない', () => {
    // 2 枚で 8 個。N=3 なら 27 個必要
    expect(isSolved(slabs, [slabPlacement(0, 0), slabPlacement(1, 1)], 3)).toBe(false);
  });

  it('不正な N は RangeError', () => {
    for (const n of [0, -1, 1.5, NaN]) {
      expect(() => isSolved(slabs, [slabPlacement(0, 0), slabPlacement(1, 1)], n)).toThrow(
        RangeError,
      );
    }
  });

  it('ピースと配置の対応が壊れていたら Error', () => {
    // 配置漏れ
    expect(() => isSolved(slabs, [slabPlacement(0, 0)], 2)).toThrow(Error);
    // 未知のピース id
    expect(() => isSolved(slabs, [slabPlacement(0, 0), slabPlacement(9, 1)], 2)).toThrow(Error);
    // 同じピースの二重配置
    expect(() => isSolved(slabs, [slabPlacement(0, 0), slabPlacement(0, 1)], 2)).toThrow(Error);
    // ピース id の重複
    const duplicated = [createPiece(0, slabVoxels), createPiece(0, slabVoxels)];
    expect(() => isSolved(duplicated, [slabPlacement(0, 0), slabPlacement(0, 1)], 2)).toThrow(
      Error,
    );
  });
});

describe('isSolved（生成結果の解答配置）', () => {
  for (const n of SIZES) {
    for (const m of representativePieceCounts(n)) {
      for (const seed of SEEDS) {
        it(`N=${n} / M=${m} / seed=${seed}: 解答配置でクリア、1 ピースを 1 マスずらすと偽`, () => {
          const { pieces, solution } = generatePuzzle(n, m, seed);
          expect(isSolved(pieces, solution, n)).toBe(true);

          // 全ピースを +x に 1 マスずらす（先頭のピースは 6 方向すべて試す）
          for (let i = 0; i < solution.length; i++) {
            const placement = solution[i];
            if (placement === undefined) throw new Error('配置が無い');
            const steps = i === 0 ? STEPS : STEPS.slice(0, 1);
            for (const step of steps) {
              const moved: Placement = { ...placement, position: addVec3(placement.position, step) };
              expect(isSolved(pieces, replaceAt(solution, i, moved), n)).toBe(false);
            }
          }
        });
      }
    }
  }

  it('立方体全体を平行移動してもクリアのまま', () => {
    for (const n of SIZES) {
      for (const shift of [vec3(1, 0, 0), vec3(-9, 4, 13), vec3(-n, -n, -n)]) {
        const { pieces, solution } = generatePuzzle(n, maxPieces(n), 5);
        expect(isSolved(pieces, translateAll(solution, shift), n)).toBe(true);
      }
    }
  });

  it('1 ピースを回すと偽（回して形が変わる場合）', () => {
    for (const n of SIZES) {
      for (const m of representativePieceCounts(n)) {
        const { pieces, solution } = generatePuzzle(n, m, 20260904);
        let checked = 0;
        for (let i = 0; i < pieces.length; i++) {
          const piece = pieces[i];
          const placement = solution[i];
          if (piece === undefined || placement === undefined) throw new Error('ピースが無い');
          if (piece.voxels.length < 2) continue;
          const original = keySet(placedVoxels(piece, placement));
          for (let orientation = 1; orientation < ORIENTATION_COUNT; orientation++) {
            const rotated: Placement = { ...placement, orientation };
            const voxels = keySet(placedVoxels(piece, rotated));
            // 回転で形が変わらない向き（対称なピース）は判定が変わらないので除く
            if (voxels.size === original.size && [...voxels].every((k): boolean => original.has(k))) {
              continue;
            }
            expect(isSolved(pieces, replaceAt(solution, i, rotated), n)).toBe(false);
            checked++;
            break;
          }
        }
        // どの N / M でも「回すと形が変わるピース」が最低 1 つはある
        expect(checked).toBeGreaterThan(0);
      }
    }
  });

  it('2 つのピースを入れ替えると偽（形が違う場合）', () => {
    const { pieces, solution } = generatePuzzle(4, 8, 314);
    const a = solution[0];
    const b = solution[1];
    const pieceA = pieces[0];
    const pieceB = pieces[1];
    if (a === undefined || b === undefined || pieceA === undefined || pieceB === undefined) {
      throw new Error('ピースが無い');
    }
    const swapped = replaceAt(
      replaceAt(solution, 0, { ...a, position: b.position }),
      1,
      { ...b, position: a.position },
    );
    expect(isSolved(pieces, swapped, 4)).toBe(false);
  });
});
