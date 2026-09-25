import { describe, expect, it } from 'vitest';
import {
  addVec3,
  compareVec3,
  IDENTITY_ORIENTATION,
  vec3,
  vec3Key,
  type Vec3,
} from '../src/core/grid';
import {
  boundingBox,
  createPiece,
  placedVoxels,
  type Piece,
  type Placement,
} from '../src/core/piece';
import { generatePuzzle } from '../src/core/generate';
import { snapCandidate } from '../src/core/solve';

/** 6 近傍のオフセット。 */
const STEPS: readonly Vec3[] = [
  vec3(1, 0, 0),
  vec3(-1, 0, 0),
  vec3(0, 1, 0),
  vec3(0, -1, 0),
  vec3(0, 0, 1),
  vec3(0, 0, -1),
];

/** 配置を組み立てる小さな入口。向きは既定で恒等。 */
function place(pieceId: number, position: Vec3, orientation = IDENTITY_ORIENTATION): Placement {
  return { pieceId, orientation, position };
}

function findPiece(pieces: readonly Piece[], id: number): Piece {
  const piece = pieces.find((p): boolean => p.id === id);
  if (piece === undefined) throw new Error(`テスト: ピース ${id} が無い`);
  return piece;
}

function findPlacement(placements: readonly Placement[], id: number): Placement {
  const placement = placements.find((p): boolean => p.pieceId === id);
  if (placement === undefined) throw new Error(`テスト: ピース ${id} の配置が無い`);
  return placement;
}

/** null なら失敗させて Placement として扱えるようにする。 */
function expectCandidate(candidate: Placement | null): Placement {
  if (candidate === null) throw new Error('テスト: 候補が見つからなかった');
  return candidate;
}

function manhattan(v: Vec3): number {
  return Math.abs(v.x) + Math.abs(v.y) + Math.abs(v.z);
}

/** アクティブ以外の全ピースのワールドボクセル。 */
function otherVoxels(
  pieces: readonly Piece[],
  placements: readonly Placement[],
  activePieceId: number,
): Vec3[] {
  const voxels: Vec3[] = [];
  for (const placement of placements) {
    if (placement.pieceId === activePieceId) continue;
    for (const v of placedVoxels(findPiece(pieces, placement.pieceId), placement)) voxels.push(v);
  }
  return voxels;
}

/**
 * 実装とは独立に SPEC.md 3.5 の 3 条件を素朴に判定する（テスト側の参照実装）。
 * 接する・重ならない・外接立方体が N×N×N に収まる。
 */
function isValidPosition(
  pieces: readonly Piece[],
  placements: readonly Placement[],
  activePieceId: number,
  n: number,
  position: Vec3,
): boolean {
  const active = findPlacement(placements, activePieceId);
  const others = otherVoxels(pieces, placements, activePieceId);
  if (others.length === 0) return false;

  const moved = placedVoxels(
    findPiece(pieces, activePieceId),
    place(activePieceId, position, active.orientation),
  );
  const occupied = new Set(others.map(vec3Key));
  for (const v of moved) {
    if (occupied.has(vec3Key(v))) return false;
  }
  let touches = false;
  for (const v of moved) {
    for (const step of STEPS) {
      if (occupied.has(vec3Key(addVec3(v, step)))) touches = true;
    }
  }
  if (!touches) return false;

  const box = boundingBox([...others, ...moved]);
  return box.size.x <= n && box.size.y <= n && box.size.z <= n;
}

/** 27 通りの平行移動を総当たりして「最も近い有効な位置」を求める参照実装。 */
function bruteForceCandidate(
  pieces: readonly Piece[],
  placements: readonly Placement[],
  activePieceId: number,
  n: number,
): Placement | null {
  const active = findPlacement(placements, activePieceId);
  let best: Vec3 | null = null;
  for (let x = -1; x <= 1; x += 1) {
    for (let y = -1; y <= 1; y += 1) {
      for (let z = -1; z <= 1; z += 1) {
        const offset = vec3(x, y, z);
        const position = addVec3(active.position, offset);
        if (!isValidPosition(pieces, placements, activePieceId, n, position)) continue;
        if (best === null) {
          best = offset;
          continue;
        }
        const closer = manhattan(offset) - manhattan(best);
        if (closer < 0 || (closer === 0 && compareVec3(offset, best) < 0)) best = offset;
      }
    }
  }
  if (best === null) return null;
  return place(activePieceId, addVec3(active.position, best), active.orientation);
}

/** x 方向に 2 マス続くドミノ（正規化後は (0,0,0) と (1,0,0)）。 */
function domino(id: number): Piece {
  return createPiece(id, [vec3(0, 0, 0), vec3(1, 0, 0)]);
}

/** x 方向に 3 マス続く棒（正規化後は (-1,0,0)・(0,0,0)・(1,0,0)）。 */
function bar3(id: number): Piece {
  return createPiece(id, [vec3(0, 0, 0), vec3(1, 0, 0), vec3(2, 0, 0)]);
}

/** 単独ボクセル。 */
function unit(id: number): Piece {
  return createPiece(id, [vec3(0, 0, 0)]);
}

describe('snapCandidate（手で組んだケース）', () => {
  const dominoes: readonly Piece[] = [domino(0), domino(1)];

  it('1 マスずれた位置なら正解位置を返す', () => {
    // ピース 0 は (0,0,0)-(1,0,0)。ピース 1 の正解は (0,1,0)-(1,1,0) だが 1 マス上にずれている
    const placements = [place(0, vec3(0, 0, 0)), place(1, vec3(0, 2, 0))];
    const candidate = expectCandidate(snapCandidate(dominoes, placements, 1, 3));
    expect(candidate.pieceId).toBe(1);
    expect(candidate.position).toEqual(vec3(0, 1, 0));
    // 向きは変えない（スナップは平行移動だけ）
    expect(candidate.orientation).toBe(IDENTITY_ORIENTATION);
  });

  it('2 マス以上離れていれば null', () => {
    const placements = [place(0, vec3(0, 0, 0)), place(1, vec3(0, 4, 0))];
    expect(snapCandidate(dominoes, placements, 1, 3)).toBeNull();
  });

  it('どのピースとも接しない位置は候補にならない', () => {
    // N=5 なので外接立方体には余裕がある。斜めに離れていて 1 マス動かしても面が接しない
    const units: readonly Piece[] = [unit(0), unit(1)];
    const placements = [place(0, vec3(0, 0, 0)), place(1, vec3(2, 2, 0))];
    expect(snapCandidate(units, placements, 1, 5)).toBeNull();
  });

  it('重なる位置は候補にならない', () => {
    // 現在位置（移動量 0）はピース 0 と 1 マス重なっている。重ならない別の位置が選ばれる
    const placements = [place(0, vec3(0, 0, 0)), place(1, vec3(1, 0, 0))];
    const candidate = expectCandidate(snapCandidate(dominoes, placements, 1, 3));
    expect(candidate.position).not.toEqual(vec3(1, 0, 0));

    const occupied = new Set(otherVoxels(dominoes, placements, 1).map(vec3Key));
    for (const v of placedVoxels(findPiece(dominoes, 1), candidate)) {
      expect(occupied.has(vec3Key(v))).toBe(false);
    }
    expect(candidate).toEqual(bruteForceCandidate(dominoes, placements, 1, 3));
  });

  it('外接立方体が N を超える位置は候補にならない', () => {
    // 3 マスの棒 2 本。現在位置のままだと x が 4 マスに広がるので、x を 1 縮める位置が選ばれる
    const bars: readonly Piece[] = [bar3(0), bar3(1)];
    const placements = [place(0, vec3(1, 0, 0)), place(1, vec3(2, 1, 0))];
    expect(isValidPosition(bars, placements, 1, 3, vec3(2, 1, 0))).toBe(false);

    const candidate = expectCandidate(snapCandidate(bars, placements, 1, 3));
    expect(candidate.position).toEqual(vec3(1, 1, 0));

    const all = [
      ...otherVoxels(bars, placements, 1),
      ...placedVoxels(findPiece(bars, 1), candidate),
    ];
    expect(boundingBox(all).size).toEqual(vec3(3, 2, 1));
  });

  it('現在位置が条件を満たしていれば移動量 0 の候補を返す', () => {
    const placements = [place(0, vec3(0, 0, 0)), place(1, vec3(0, 1, 0))];
    const candidate = expectCandidate(snapCandidate(dominoes, placements, 1, 3));
    expect(candidate.position).toEqual(vec3(0, 1, 0));
  });

  it('接する相手がいなければ null', () => {
    const single: readonly Piece[] = [domino(0)];
    expect(snapCandidate(single, [place(0, vec3(0, 0, 0))], 0, 3)).toBeNull();
  });

  it('候補が複数あるときは最も近いものを決定的に選ぶ', () => {
    // ピース 1 は斜め位置。(-1,0,0) と (0,-1,0) がどちらも有効で距離は同じ。
    // 同点は座標の辞書順（x → y → z）なので (-1,0,0) が選ばれる
    const units: readonly Piece[] = [unit(0), unit(1)];
    const placements = [place(0, vec3(0, 0, 0)), place(1, vec3(1, 1, 0))];
    expect(isValidPosition(units, placements, 1, 3, vec3(0, 1, 0))).toBe(true);
    expect(isValidPosition(units, placements, 1, 3, vec3(1, 0, 0))).toBe(true);

    const candidate = expectCandidate(snapCandidate(units, placements, 1, 3));
    expect(candidate.position).toEqual(vec3(0, 1, 0));

    // 同じ入力なら何度呼んでも同じ。ピースと配置の並びを変えても変わらない
    expect(snapCandidate(units, placements, 1, 3)).toEqual(candidate);
    expect(snapCandidate(units, [...placements].reverse(), 1, 3)).toEqual(candidate);
    expect(snapCandidate([...units].reverse(), placements, 1, 3)).toEqual(candidate);
  });

  it('遠い候補より近い候補を選ぶ', () => {
    // (0,-1,0) は距離 1、(0,-1,±1) や (±1,-1,0) は距離 2。どれも有効だが近い方が返る
    const units: readonly Piece[] = [unit(0), unit(1)];
    const placements = [place(0, vec3(0, 0, 0)), place(1, vec3(0, 2, 0))];
    expect(isValidPosition(units, placements, 1, 3, vec3(0, 1, 0))).toBe(true);
    expect(isValidPosition(units, placements, 1, 3, vec3(1, 1, 0))).toBe(false);
    expect(isValidPosition(units, placements, 1, 3, vec3(0, 0, 1))).toBe(true);

    const candidate = expectCandidate(snapCandidate(units, placements, 1, 3));
    expect(candidate.position).toEqual(vec3(0, 1, 0));
  });

  it('入力が壊れていれば例外を投げる', () => {
    const placements = [place(0, vec3(0, 0, 0)), place(1, vec3(0, 2, 0))];
    expect((): unknown => snapCandidate(dominoes, placements, 9, 3)).toThrow();
    expect((): unknown => snapCandidate(dominoes, placements, 1, 0)).toThrow(RangeError);
    expect((): unknown =>
      snapCandidate(dominoes, [...placements, place(1, vec3(5, 5, 5))], 1, 3),
    ).toThrow();
  });
});

describe('snapCandidate（生成したパズル）', () => {
  const CASES: readonly { n: number; m: number; seed: number }[] = [
    { n: 3, m: 2, seed: 1 },
    { n: 3, m: 4, seed: 7 },
    { n: 4, m: 5, seed: 12345 },
    { n: 5, m: 8, seed: 99 },
    { n: 7, m: 27, seed: 2026 },
  ];

  for (const { n, m, seed } of CASES) {
    it(`N=${n} M=${m} seed=${seed}: 1 マスずらしても総当たりと同じ答えになる`, () => {
      const puzzle = generatePuzzle(n, m, seed);
      for (const step of STEPS) {
        for (const target of puzzle.solution) {
          const placements = puzzle.solution.map((placement): Placement =>
            placement.pieceId === target.pieceId
              ? place(placement.pieceId, addVec3(placement.position, step), placement.orientation)
              : placement,
          );
          const candidate = snapCandidate(puzzle.pieces, placements, target.pieceId, n);
          // 総当たりの参照実装と完全に一致する
          expect(candidate).toEqual(
            bruteForceCandidate(puzzle.pieces, placements, target.pieceId, n),
          );
          const found = expectCandidate(candidate);

          // 他のピースが N×N×N を張っているなら、収まる位置は解答位置しか無い
          const box = boundingBox(otherVoxels(puzzle.pieces, placements, target.pieceId));
          if (box.size.x === n && box.size.y === n && box.size.z === n) {
            expect(found.position).toEqual(target.position);
          }
        }
      }
    });
  }

  it('立方体から遠く離れた配置では候補が出ない', () => {
    const puzzle = generatePuzzle(4, 5, 3);
    const placements = puzzle.solution.map((placement): Placement =>
      placement.pieceId === 0
        ? place(
            placement.pieceId,
            addVec3(placement.position, vec3(20, 20, 20)),
            placement.orientation,
          )
        : placement,
    );
    expect(snapCandidate(puzzle.pieces, placements, 0, 4)).toBeNull();
  });
});
