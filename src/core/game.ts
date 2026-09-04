// ゲーム状態の集約（SPEC.md 3.3 / 3.4）。現在の配置を持ち、変更のたびにクリア判定を回す。
// Three.js に依存しない純粋なロジック。描画への反映は onChange のコールバックで外へ出す。

import { addVec3, rotateOrientation, type Axis, type Vec3 } from './grid';
import type { Piece, Placement } from './piece';
import { isSolved } from './solve';

/** 配置を delta だけ平行移動した新しい配置。向きは変えない。 */
export function movePlacement(placement: Placement, delta: Vec3): Placement {
  return {
    pieceId: placement.pieceId,
    orientation: placement.orientation,
    position: addVec3(placement.position, delta),
  };
}

/**
 * 軸まわりに 90 度回した新しい配置。
 * position はピースの局所原点のワールド座標なので、向き id を差し替えるだけで
 * 「局所原点を中心に回す」（SPEC.md 3.3）になる。
 */
export function rotatePlacement(placement: Placement, axis: Axis, dir: 1 | -1): Placement {
  return {
    pieceId: placement.pieceId,
    orientation: rotateOrientation(placement.orientation, axis, dir),
    position: placement.position,
  };
}

/** placements のうち next と同じ id の配置を差し替えた新しい配列。元の並びは保つ。 */
export function replacePlacement(
  placements: readonly Placement[],
  next: Placement,
): Placement[] {
  let found = false;
  const result = placements.map((placement): Placement => {
    if (placement.pieceId !== next.pieceId) return placement;
    found = true;
    return next;
  });
  if (!found) throw new Error(`配置の差し替え: 未知のピース id ${next.pieceId}`);
  return result;
}

/** 配置が変わるたびに呼ばれる。solved はその時点のクリア判定の結果。 */
export type GameChangeListener = (placements: readonly Placement[], solved: boolean) => void;

/** 配置の保持と更新。すべての更新経路がクリア判定を通る。 */
export type Game = {
  readonly pieces: readonly Piece[];
  readonly n: number;
  /** 現在の全ピースの配置（順不同ではなく初期配置の並びを保つ）。 */
  placements(): readonly Placement[];
  /** ピース id の配置。未知の id なら undefined。 */
  placementOf(pieceId: number): Placement | undefined;
  /** 直近の更新時点のクリア判定。 */
  solved(): boolean;
  /** ピースを delta だけ動かす。 */
  move(pieceId: number, delta: Vec3): void;
  /** ピースを軸まわりに 90 度回す。 */
  rotate(pieceId: number, axis: Axis, dir: 1 | -1): void;
  /** 配置をまるごと入れ替える（「散らし直す」）。 */
  reset(placements: readonly Placement[]): void;
};

/**
 * ゲーム状態を作る。
 * 更新のたびに `isSolved` を呼ぶ（SPEC.md 3.4「判定はピースを動かすたびに実行」）。
 * onChange は構築時には呼ばない。初回の描画は呼び出し側が placements() から行う。
 */
export function createGame(
  pieces: readonly Piece[],
  n: number,
  initial: readonly Placement[],
  onChange?: GameChangeListener,
): Game {
  const list: readonly Piece[] = pieces.slice();

  /** 配置の集合が全ピースをちょうど 1 回ずつ含むか確かめる。 */
  const validate = (placements: readonly Placement[]): Placement[] => {
    const known = new Set(list.map((piece): number => piece.id));
    const seen = new Set<number>();
    for (const placement of placements) {
      if (!known.has(placement.pieceId)) {
        throw new Error(`配置: 未知のピース id ${placement.pieceId}`);
      }
      if (seen.has(placement.pieceId)) {
        throw new Error(`配置: ピース id ${placement.pieceId} が重複している`);
      }
      seen.add(placement.pieceId);
    }
    if (seen.size !== known.size) {
      throw new Error(`配置の数が足りない (ピース ${known.size} / 配置 ${seen.size})`);
    }
    return placements.slice();
  };

  let current: readonly Placement[] = validate(initial);
  let solvedFlag = isSolved(list, current, n);

  const apply = (next: readonly Placement[]): void => {
    current = next;
    solvedFlag = isSolved(list, current, n);
    if (onChange) onChange(current, solvedFlag);
  };

  const placementFor = (pieceId: number): Placement => {
    const placement = current.find((p): boolean => p.pieceId === pieceId);
    if (placement === undefined) throw new Error(`未知のピース id ${pieceId}`);
    return placement;
  };

  return {
    pieces: list,
    n,
    placements(): readonly Placement[] {
      return current;
    },
    placementOf(pieceId: number): Placement | undefined {
      return current.find((p): boolean => p.pieceId === pieceId);
    },
    solved(): boolean {
      return solvedFlag;
    },
    move(pieceId: number, delta: Vec3): void {
      apply(replacePlacement(current, movePlacement(placementFor(pieceId), delta)));
    },
    rotate(pieceId: number, axis: Axis, dir: 1 | -1): void {
      apply(replacePlacement(current, rotatePlacement(placementFor(pieceId), axis, dir)));
    },
    reset(placements: readonly Placement[]): void {
      apply(validate(placements));
    },
  };
}
