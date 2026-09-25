import { describe, expect, it } from 'vitest';
import {
  addVec3,
  IDENTITY_ORIENTATION,
  ORIENTATION_COUNT,
  vec3,
  vec3Key,
  type Vec3,
} from '../src/core/grid';
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
  it('SPEC.md 3.1 の上限 N + 4(N−2) に一致する', () => {
    expect(maxPieces(3)).toBe(7);
    expect(maxPieces(4)).toBe(12);
    expect(maxPieces(5)).toBe(17);
    expect(maxPieces(6)).toBe(22);
    expect(maxPieces(7)).toBe(27);
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

describe('scatterPlacements のオプション', () => {
  const N = 4;
  const M = 5;
  const SEED = 20260906;

  /** テスト用のパズル。pieces と solution を対で返す。 */
  function puzzle(): { pieces: readonly Piece[]; solution: readonly Placement[] } {
    const generated = generatePuzzle(N, M, SEED);
    return { pieces: generated.pieces, solution: generated.solution };
  }

  /** 配置を id で引く。 */
  function pick(placements: readonly Placement[], pieceId: number): Placement {
    const found = placements.find((p): boolean => p.pieceId === pieceId);
    if (found === undefined) throw new Error(`テスト: ピース ${pieceId} の配置が無い`);
    return found;
  }

  it('省略時は従来どおり（3 引数の呼び出しと同じ結果）', () => {
    const { pieces } = puzzle();
    expect(scatterPlacements(pieces, N, SEED, {})).toEqual(scatterPlacements(pieces, N, SEED));
    expect(scatterPlacements(pieces, N, SEED, { allowRotation: true })).toEqual(
      scatterPlacements(pieces, N, SEED),
    );
  });

  it('allowRotation: false なら全配置の向きが IDENTITY_ORIENTATION', () => {
    for (const n of [3, 4, 5]) {
      const pieces = generatePieces(n, 4, 777 + n);
      const placements = scatterPlacements(pieces, n, 999, { allowRotation: false });
      expect(placements).toHaveLength(pieces.length);
      for (const placement of placements) {
        expect(placement.orientation).toBe(IDENTITY_ORIENTATION);
      }
    }
  });

  it('allowRotation: false でもピース同士は重ならない', () => {
    const pieces = generatePieces(4, 6, 4242);
    const placements = scatterPlacements(pieces, 4, 4242, { allowRotation: false });
    const world = worldVoxels(pieces, placements).map(vec3Key);
    expect(new Set(world).size).toBe(world.length);
  });

  it('keep に渡した配置はそのまま返り、他のピースと重ならない', () => {
    const { pieces, solution } = puzzle();
    for (const keep of [[pick(solution, 0)], [pick(solution, 0), pick(solution, 2)]]) {
      const placements = scatterPlacements(pieces, N, SEED, { keep });

      // keep はそのまま含まれる
      for (const kept of keep) {
        expect(pick(placements, kept.pieceId)).toEqual(kept);
      }

      // 固定ピースのボクセルに他のピースが重ならない
      const keptIds = new Set(keep.map((p): number => p.pieceId));
      const keptKeys = new Set(
        worldVoxels(pieces, keep).map(vec3Key),
      );
      const others = placements.filter((p): boolean => !keptIds.has(p.pieceId));
      for (const key of worldVoxels(pieces, others).map(vec3Key)) {
        expect(keptKeys.has(key)).toBe(false);
      }
    }
  });

  it('keep 込みでも全ピースがちょうど 1 回ずつ現れ、ボクセルの重複が無い', () => {
    const { pieces, solution } = puzzle();
    const placements = scatterPlacements(pieces, N, SEED, {
      keep: [pick(solution, 1), pick(solution, 3)],
    });

    expect(placements).toHaveLength(pieces.length);
    expect(placements.map((p): number => p.pieceId)).toEqual(pieces.map((p): number => p.id));

    const world = worldVoxels(pieces, placements).map(vec3Key);
    expect(new Set(world).size).toBe(world.length);
  });

  it('keep と allowRotation: false を同時に使える', () => {
    const { pieces, solution } = puzzle();
    const keep = [pick(solution, 0)];
    const placements = scatterPlacements(pieces, N, SEED, { keep, allowRotation: false });
    for (const placement of placements) {
      expect(placement.orientation).toBe(IDENTITY_ORIENTATION);
    }
    expect(pick(placements, 0)).toEqual(pick(solution, 0));
  });

  it('同じ引数の 2 回の呼び出しは等しい（決定的）', () => {
    const { pieces, solution } = puzzle();
    const options = { keep: [pick(solution, 2)], allowRotation: false };
    expect(scatterPlacements(pieces, N, SEED, options)).toEqual(
      scatterPlacements(pieces, N, SEED, options),
    );
    const rotated = { keep: [pick(solution, 2)] };
    expect(scatterPlacements(pieces, N, SEED, rotated)).toEqual(
      scatterPlacements(pieces, N, SEED, rotated),
    );
  });

  it('keep が全ピース分でも例外にならず、その配置がそのまま返る', () => {
    const { pieces, solution } = puzzle();
    // 解答そのものはクリア状態だが、keep があるときは引き直しの上限を超えても例外にしない
    const placements = scatterPlacements(pieces, N, SEED, { keep: solution });
    expect(placements).toEqual(solution.slice());
  });

  it('keep の未知 id / 重複 id は例外', () => {
    const { pieces, solution } = puzzle();
    const unknown: Placement = { pieceId: 999, orientation: 0, position: vec3(0, 0, 0) };
    expect(() => scatterPlacements(pieces, N, SEED, { keep: [unknown] })).toThrow();
    expect(() =>
      scatterPlacements(pieces, N, SEED, { keep: [pick(solution, 0), pick(solution, 0)] }),
    ).toThrow();
  });
});
