import { describe, expect, it, vi } from 'vitest';
import { equalsVec3, rotateOrientation, vec3, vec3Key, type Axis, type Vec3 } from '../src/core/grid';
import { placedVoxels, type Piece, type Placement } from '../src/core/piece';
import { generatePuzzle, scatterPlacements } from '../src/core/generate';
import {
  createGame,
  movePlacement,
  replacePlacement,
  rotatePlacement,
} from '../src/core/game';
import { isSolved } from '../src/core/solve';

const N = 3;
const M = 4;
const SEED = 20260904;

/** テスト用のパズル一式。 */
function puzzle(): { pieces: readonly Piece[]; solution: readonly Placement[] } {
  const generated = generatePuzzle(N, M, SEED);
  return { pieces: generated.pieces, solution: generated.solution };
}

/** 配列から id で配置を引く。 */
function placementOf(placements: readonly Placement[], pieceId: number): Placement {
  const found = placements.find((p): boolean => p.pieceId === pieceId);
  if (found === undefined) throw new Error(`テスト: ピース ${pieceId} の配置が無い`);
  return found;
}

/** 1 マス分の移動ベクトル。 */
function unitStep(axis: Axis, sign: 1 | -1): Vec3 {
  if (axis === 'x') return vec3(sign, 0, 0);
  if (axis === 'y') return vec3(0, sign, 0);
  return vec3(0, 0, sign);
}

/** UI の回転ボタン 6 個だけを使って from → to へ辿る手順を幅優先で求める。 */
function rotationPath(from: number, to: number): { axis: Axis; dir: 1 | -1 }[] {
  if (from === to) return [];
  const steps: { axis: Axis; dir: 1 | -1 }[] = [
    { axis: 'x', dir: 1 },
    { axis: 'x', dir: -1 },
    { axis: 'y', dir: 1 },
    { axis: 'y', dir: -1 },
    { axis: 'z', dir: 1 },
    { axis: 'z', dir: -1 },
  ];
  const previous = new Map<number, { orientation: number; step: { axis: Axis; dir: 1 | -1 } }>();
  const queue: number[] = [from];
  const seen = new Set<number>([from]);
  while (queue.length > 0) {
    const current = queue.shift();
    if (current === undefined) break;
    for (const step of steps) {
      const next = rotateOrientation(current, step.axis, step.dir);
      if (seen.has(next)) continue;
      seen.add(next);
      previous.set(next, { orientation: current, step });
      if (next === to) {
        const path: { axis: Axis; dir: 1 | -1 }[] = [];
        let node = to;
        for (;;) {
          const entry = previous.get(node);
          if (entry === undefined) break;
          path.unshift(entry.step);
          node = entry.orientation;
        }
        return path;
      }
      queue.push(next);
    }
  }
  throw new Error(`テスト: 向き ${from} から ${to} へ辿れない`);
}

describe('movePlacement', () => {
  it('位置に delta を足し、id と向きは変えない', () => {
    const before: Placement = { pieceId: 2, orientation: 7, position: vec3(1, -2, 3) };
    const after = movePlacement(before, vec3(0, 1, -1));
    expect(after.pieceId).toBe(2);
    expect(after.orientation).toBe(7);
    expect(after.position).toEqual(vec3(1, -1, 2));
    // 元の配置は書き換えない
    expect(before.position).toEqual(vec3(1, -2, 3));
  });

  it('ゼロ移動は同じ位置になる', () => {
    const before: Placement = { pieceId: 0, orientation: 0, position: vec3(4, 5, 6) };
    expect(movePlacement(before, vec3(0, 0, 0)).position).toEqual(before.position);
  });
});

describe('rotatePlacement', () => {
  it('同じ軸に 4 回まわすと元の向きに戻る', () => {
    const start: Placement = { pieceId: 1, orientation: 5, position: vec3(2, 0, -3) };
    for (const axis of ['x', 'y', 'z'] as const) {
      for (const dir of [1, -1] as const) {
        let current = start;
        for (let i = 0; i < 4; i++) current = rotatePlacement(current, axis, dir);
        expect(current.orientation).toBe(start.orientation);
        expect(current.position).toEqual(start.position);
      }
    }
  });

  it('+1 と -1 は打ち消し合う', () => {
    const start: Placement = { pieceId: 0, orientation: 11, position: vec3(0, 0, 0) };
    expect(rotatePlacement(rotatePlacement(start, 'y', 1), 'y', -1).orientation).toBe(11);
  });

  it('局所原点（= position のマス）は回転しても動かない', () => {
    const { pieces, solution } = puzzle();
    const piece = pieces[0];
    const placement = solution[0];
    if (!piece || !placement) throw new Error('テスト: ピースが無い');
    const rotated = rotatePlacement(placement, 'x', 1);
    // 局所原点は (0,0,0) なので、回した後も position のマスを必ず占める
    const occupied = placedVoxels(piece, rotated).map(vec3Key);
    expect(occupied).toContain(vec3Key(placement.position));
    // ボクセル数は変わらない
    expect(occupied.length).toBe(piece.voxels.length);
  });
});

describe('replacePlacement', () => {
  it('対象だけを差し替え、並びは保つ', () => {
    const list: Placement[] = [
      { pieceId: 0, orientation: 0, position: vec3(0, 0, 0) },
      { pieceId: 1, orientation: 0, position: vec3(1, 0, 0) },
      { pieceId: 2, orientation: 0, position: vec3(2, 0, 0) },
    ];
    const next: Placement = { pieceId: 1, orientation: 3, position: vec3(9, 9, 9) };
    const result = replacePlacement(list, next);
    expect(result.map((p): number => p.pieceId)).toEqual([0, 1, 2]);
    expect(result[1]).toBe(next);
    expect(result[0]).toBe(list[0]);
    expect(list[1]?.orientation).toBe(0);
  });

  it('未知の id は例外', () => {
    const list: Placement[] = [{ pieceId: 0, orientation: 0, position: vec3(0, 0, 0) }];
    expect(() =>
      replacePlacement(list, { pieceId: 5, orientation: 0, position: vec3(0, 0, 0) }),
    ).toThrow();
  });
});

describe('createGame', () => {
  it('解答配置なら solved が真、1 マスずらすと偽になる', () => {
    const { pieces, solution } = puzzle();
    const game = createGame(pieces, N, solution);
    expect(game.solved()).toBe(true);
    game.move(0, vec3(1, 0, 0));
    expect(game.solved()).toBe(false);
    game.move(0, vec3(-1, 0, 0));
    expect(game.solved()).toBe(true);
  });

  it('散らし配置から解答配置に戻すとクリアになる', () => {
    const { pieces, solution } = puzzle();
    const scattered = scatterPlacements(pieces, N, SEED);
    const game = createGame(pieces, N, scattered);
    expect(game.solved()).toBe(false);
    game.reset(solution);
    expect(game.solved()).toBe(true);
  });

  it('動かすたびに onChange がクリア判定つきで呼ばれる', () => {
    const { pieces, solution } = puzzle();
    const onChange = vi.fn();
    const game = createGame(pieces, N, solution, onChange);
    // 構築時には呼ばない
    expect(onChange).not.toHaveBeenCalled();

    game.move(1, vec3(0, 5, 0));
    expect(onChange).toHaveBeenCalledTimes(1);
    expect(onChange.mock.calls[0]?.[1]).toBe(false);

    game.rotate(1, 'y', 1);
    expect(onChange).toHaveBeenCalledTimes(2);

    game.reset(solution);
    expect(onChange).toHaveBeenCalledTimes(3);
    expect(onChange.mock.calls[2]?.[1]).toBe(true);
  });

  it('onChange に渡る配置は placements() と一致し、判定は isSolved と同じ', () => {
    const { pieces, solution } = puzzle();
    let latest: readonly Placement[] = [];
    const game = createGame(pieces, N, solution, (placements): void => {
      latest = placements;
    });
    game.move(2, vec3(-3, 1, 2));
    expect(latest).toEqual(game.placements());
    expect(game.solved()).toBe(isSolved(pieces, game.placements(), N));
  });

  it('move / rotate は対象以外の配置を変えない', () => {
    const { pieces, solution } = puzzle();
    const game = createGame(pieces, N, solution);
    const others = solution
      .filter((p): boolean => p.pieceId !== 0)
      .map((p): Placement => ({ ...p }));
    game.move(0, vec3(2, -1, 0));
    game.rotate(0, 'z', -1);
    for (const before of others) {
      const after = placementOf(game.placements(), before.pieceId);
      expect(after.orientation).toBe(before.orientation);
      expect(equalsVec3(after.position, before.position)).toBe(true);
    }
  });

  it('placementOf は現在の配置を返し、未知の id では undefined', () => {
    const { pieces, solution } = puzzle();
    const game = createGame(pieces, N, solution);
    const moved: Vec3 = vec3(1, 2, 3);
    game.move(0, moved);
    const placement = game.placementOf(0);
    expect(placement).toBeDefined();
    expect(placement?.position).toEqual(
      vec3(
        placementOf(solution, 0).position.x + moved.x,
        placementOf(solution, 0).position.y + moved.y,
        placementOf(solution, 0).position.z + moved.z,
      ),
    );
    expect(game.placementOf(999)).toBeUndefined();
  });

  it('散らし配置から ±90 度回転と 1 マス移動だけでクリアまで持っていける', () => {
    const { pieces, solution } = puzzle();
    const scattered = scatterPlacements(pieces, N, SEED);
    const game = createGame(pieces, N, scattered);
    expect(game.solved()).toBe(false);

    for (const target of solution) {
      // 回転: 現在の向きから目標の向きまでを ±90 度の 1 手ずつで辿る（UI ボタンと同じ操作）
      const start = placementOf(game.placements(), target.pieceId).orientation;
      for (const step of rotationPath(start, target.orientation)) {
        game.rotate(target.pieceId, step.axis, step.dir);
      }
      expect(placementOf(game.placements(), target.pieceId).orientation).toBe(target.orientation);

      // 移動: 1 マスずつ（ドラッグ 1 段分と同じ粒度）
      for (const axis of ['x', 'y', 'z'] as const) {
        for (;;) {
          const now = placementOf(game.placements(), target.pieceId).position;
          const diff = target.position[axis] - now[axis];
          if (diff === 0) break;
          game.move(target.pieceId, unitStep(axis, diff > 0 ? 1 : -1));
        }
      }
    }
    expect(game.solved()).toBe(true);
  });

  it('未知の id の操作と、数の合わない reset は例外', () => {
    const { pieces, solution } = puzzle();
    const game = createGame(pieces, N, solution);
    expect(() => game.move(999, vec3(1, 0, 0))).toThrow();
    expect(() => game.rotate(999, 'x', 1)).toThrow();
    expect(() => game.reset(solution.slice(1))).toThrow();
    expect(() =>
      game.reset([...solution.slice(1), placementOf(solution, 1)]),
    ).toThrow();
  });
});
