import { describe, expect, it } from 'vitest';
import { addVec3, ORIENTATION_COUNT, vec3, vec3Key, type Vec3 } from '../src/core/grid';
import { placedVoxels, type Piece, type Placement } from '../src/core/piece';
import {
  generatePieces,
  generatePuzzle,
  MAX_SPACE_SIZE,
  maxPieces,
  MIN_PIECE_COUNT,
  MIN_SPACE_SIZE,
  scatterPlacements,
  solutionPlacements,
} from '../src/core/generate';
import { isSolved } from '../src/core/solve';

/** テスト側で独立に定義する 6 近傍。 */
const NEIGHBORS: readonly Vec3[] = [
  vec3(1, 0, 0),
  vec3(-1, 0, 0),
  vec3(0, 1, 0),
  vec3(0, -1, 0),
  vec3(0, 0, 1),
  vec3(0, 0, -1),
];

const SIZES: readonly number[] = [3, 4, 5, 6, 7];
const SEEDS: readonly number[] = [1, 7, 12345, 2026];

/** M の代表値（下限・中間・上限）。 */
function representativePieceCounts(n: number): number[] {
  const limit = maxPieces(n);
  const middle = Math.floor((MIN_PIECE_COUNT + limit) / 2);
  return [...new Set([MIN_PIECE_COUNT, middle, limit])];
}

/** ボクセル集合が 6 近傍で連結しているか（BFS）。 */
function isConnected(voxels: readonly Vec3[]): boolean {
  const start = voxels[0];
  if (start === undefined) return false;
  const all = new Set(voxels.map(vec3Key));
  const seen = new Set<string>([vec3Key(start)]);
  const queue: Vec3[] = [start];
  for (let head = 0; head < queue.length; head++) {
    const v = queue[head];
    if (v === undefined) continue;
    for (const offset of NEIGHBORS) {
      const neighbor = addVec3(v, offset);
      const key = vec3Key(neighbor);
      if (!all.has(key) || seen.has(key)) continue;
      seen.add(key);
      queue.push(neighbor);
    }
  }
  return seen.size === all.size;
}

/** ピースと配置を id で対応付けて、全ワールド座標を集める。 */
function worldVoxels(pieces: readonly Piece[], placements: readonly Placement[]): Vec3[] {
  const world: Vec3[] = [];
  for (const placement of placements) {
    const piece = pieces.find((p): boolean => p.id === placement.pieceId);
    if (piece === undefined) throw new Error(`テスト: ピース ${placement.pieceId} が無い`);
    for (const v of placedVoxels(piece, placement)) world.push(v);
  }
  return world;
}

describe('maxPieces', () => {
  it('SPEC.md 3.1 の目安（N=3 で 6、N=7 で 40）に一致する', () => {
    expect(maxPieces(3)).toBe(6);
    expect(maxPieces(4)).toBe(16);
    expect(maxPieces(5)).toBe(31);
    expect(maxPieces(6)).toBe(40);
    expect(maxPieces(7)).toBe(40);
  });

  it('どの N でも下限 2 以上で N³ 以下', () => {
    for (const n of SIZES) {
      expect(maxPieces(n)).toBeGreaterThanOrEqual(MIN_PIECE_COUNT);
      expect(maxPieces(n)).toBeLessThanOrEqual(n ** 3);
    }
  });

  it('範囲外の N は RangeError', () => {
    for (const n of [MIN_SPACE_SIZE - 1, MAX_SPACE_SIZE + 1, 0, -3, 3.5, NaN, Infinity]) {
      expect(() => maxPieces(n)).toThrow(RangeError);
    }
  });
});

describe('generatePuzzle', () => {
  for (const n of SIZES) {
    for (const m of representativePieceCounts(n)) {
      for (const seed of SEEDS) {
        it(`N=${n} / M=${m} / seed=${seed}: 全ボクセルがちょうど 1 つのピースに属し、各ピースが連結`, () => {
          const puzzle = generatePuzzle(n, m, seed);
          expect(puzzle.n).toBe(n);
          expect(puzzle.m).toBe(m);
          expect(puzzle.seed).toBe(seed);
          expect(puzzle.pieces).toHaveLength(m);
          expect(puzzle.solution).toHaveLength(m);

          // ピース id は 0..M-1
          expect(puzzle.pieces.map((p): number => p.id)).toEqual(
            Array.from({ length: m }, (_, i): number => i),
          );

          const occurrence = new Map<string, number>();
          for (let i = 0; i < m; i++) {
            const piece = puzzle.pieces[i];
            const placement = puzzle.solution[i];
            if (piece === undefined || placement === undefined) throw new Error('ピースが無い');

            // 局所原点が (0,0,0) に正規化されている
            expect(piece.voxels.some((v): boolean => v.x === 0 && v.y === 0 && v.z === 0)).toBe(
              true,
            );
            // 解答配置は向きが恒等
            expect(placement.orientation).toBe(0);

            const world = placedVoxels(piece, placement);
            expect(world.length).toBeGreaterThan(0);
            // 6 近傍で連結している
            expect(isConnected(world)).toBe(true);
            // 解答位置は N×N×N の中に収まる
            const inside = world.every(
              (v): boolean =>
                v.x >= 0 && v.x < n && v.y >= 0 && v.y < n && v.z >= 0 && v.z < n,
            );
            expect(inside).toBe(true);

            for (const v of world) {
              const key = vec3Key(v);
              occurrence.set(key, (occurrence.get(key) ?? 0) + 1);
            }
          }

          // 欠けなし（N³ マスすべて）・重複なし（どのマスもちょうど 1 回）
          expect(occurrence.size).toBe(n ** 3);
          expect([...occurrence.values()].every((c): boolean => c === 1)).toBe(true);
        });
      }
    }
  }

  it('同じ引数なら結果が完全に一致する（決定的）', () => {
    for (const n of SIZES) {
      for (const m of representativePieceCounts(n)) {
        expect(generatePuzzle(n, m, 4649)).toEqual(generatePuzzle(n, m, 4649));
      }
    }
  });

  it('シードが違えば別の分割になる', () => {
    const a = generatePuzzle(5, 8, 1);
    const b = generatePuzzle(5, 8, 2);
    expect(a).not.toEqual(b);
  });

  it('不正な N は RangeError', () => {
    for (const n of [MIN_SPACE_SIZE - 1, MAX_SPACE_SIZE + 1, 0, -1, 4.5, NaN]) {
      expect(() => generatePuzzle(n, 2, 1)).toThrow(RangeError);
    }
  });

  it('不正な M は RangeError', () => {
    for (const n of SIZES) {
      expect(() => generatePuzzle(n, MIN_PIECE_COUNT - 1, 1)).toThrow(RangeError);
      expect(() => generatePuzzle(n, 0, 1)).toThrow(RangeError);
      expect(() => generatePuzzle(n, -2, 1)).toThrow(RangeError);
      expect(() => generatePuzzle(n, maxPieces(n) + 1, 1)).toThrow(RangeError);
      expect(() => generatePuzzle(n, 2.5, 1)).toThrow(RangeError);
      expect(() => generatePuzzle(n, NaN, 1)).toThrow(RangeError);
    }
  });

  it('不正なシードは RangeError', () => {
    expect(() => generatePuzzle(3, 2, 1.5)).toThrow(RangeError);
    expect(() => generatePuzzle(3, 2, NaN)).toThrow(RangeError);
  });
});

describe('generatePieces / solutionPlacements', () => {
  it('generatePuzzle と同じものを返す', () => {
    for (const n of SIZES) {
      const m = representativePieceCounts(n)[1] ?? MIN_PIECE_COUNT;
      const puzzle = generatePuzzle(n, m, 31);
      expect(generatePieces(n, m, 31)).toEqual(puzzle.pieces);
      expect(solutionPlacements(n, m, 31)).toEqual(puzzle.solution);
    }
  });

  it('返した配列を書き換えても次の呼び出しに影響しない', () => {
    const pieces = generatePieces(3, 4, 5);
    pieces.pop();
    expect(generatePieces(3, 4, 5)).toHaveLength(4);
  });
});

describe('scatterPlacements', () => {
  for (const n of SIZES) {
    for (const m of [MIN_PIECE_COUNT, maxPieces(n)]) {
      for (const seed of [1, 99]) {
        it(`N=${n} / M=${m} / seed=${seed}: 重ならず、向きは 0..23、クリアではない`, () => {
          const pieces = generatePieces(n, m, seed);
          const placements = scatterPlacements(pieces, n, seed + 1);

          expect(placements).toHaveLength(m);
          expect([...placements].map((p): number => p.pieceId).sort((a, b): number => a - b)).toEqual(
            pieces.map((p): number => p.id),
          );

          const validOrientation = placements.every(
            (p): boolean =>
              Number.isInteger(p.orientation) &&
              p.orientation >= 0 &&
              p.orientation < ORIENTATION_COUNT,
          );
          expect(validOrientation).toBe(true);

          const validPosition = placements.every(
            (p): boolean =>
              Number.isInteger(p.position.x) &&
              Number.isInteger(p.position.y) &&
              Number.isInteger(p.position.z),
          );
          expect(validPosition).toBe(true);

          // ピース同士が重ならない（ワールド座標の重複が無い）
          const world = worldVoxels(pieces, placements);
          expect(world).toHaveLength(n ** 3);
          expect(new Set(world.map(vec3Key)).size).toBe(n ** 3);

          // 散らした直後にクリアになっていない
          expect(isSolved(pieces, placements, n)).toBe(false);
        });
      }
    }
  }

  it('同じ seed なら同じ配置（決定的）', () => {
    for (const n of SIZES) {
      const pieces = generatePieces(n, maxPieces(n), 3);
      expect(scatterPlacements(pieces, n, 777)).toEqual(scatterPlacements(pieces, n, 777));
    }
  });

  it('seed が違えば別の配置になる', () => {
    const pieces = generatePieces(4, 6, 3);
    expect(scatterPlacements(pieces, 4, 1)).not.toEqual(scatterPlacements(pieces, 4, 2));
  });

  it('散らす範囲は立方体の周囲 ±(N+2) 程度に収まる', () => {
    for (const n of SIZES) {
      const pieces = generatePieces(n, maxPieces(n), 11);
      const placements = scatterPlacements(pieces, n, 12);
      const center = Math.floor((n - 1) / 2);
      const half = n + 2;
      const inRange = placements.every(
        (p): boolean =>
          Math.abs(p.position.x - center) <= half &&
          Math.abs(p.position.y - center) <= half &&
          Math.abs(p.position.z - center) <= half,
      );
      expect(inRange).toBe(true);
    }
  });

  it('不正な引数は RangeError', () => {
    const pieces = generatePieces(3, 2, 1);
    expect(() => scatterPlacements(pieces, 2, 1)).toThrow(RangeError);
    expect(() => scatterPlacements(pieces, 8, 1)).toThrow(RangeError);
    expect(() => scatterPlacements(pieces, 3, 0.5)).toThrow(RangeError);
    expect(() => scatterPlacements([], 3, 1)).toThrow(RangeError);
  });
});
