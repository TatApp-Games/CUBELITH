// ゲーム状態の集約（RULES.md 3.3 / 3.4）。現在の配置を持ち、変更のたびにクリア判定を回す。
// Three.js に依存しない純粋なロジック。描画への反映は onChange のコールバックで外へ出す。

import { addVec3, orientationMatrix, rotateOrientation, type Axis, type Vec3 } from './grid';
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
 * 「局所原点を中心に回す」（RULES.md 3.3）になる。
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

/**
 * 固定（ロック）の種類。
 * 'manual' は人が固定したもので unlock で解除できる。'hint' はヒントで正解位置へ送った印で解除できない。
 */
export type LockKind = 'manual' | 'hint';

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
  /** 位置と向きを直接指定して置く（ヒントの適用・回転スナップの確定に使う）。 */
  place(pieceId: number, orientation: number, position: Vec3): void;
  /** ピースを固定する。固定中は move / rotate / place が効かない。 */
  lock(pieceId: number, kind: LockKind): void;
  /** 'manual' の固定を解除する。'hint' の固定は解除できず何も起きない。 */
  unlock(pieceId: number): void;
  /** 固定の種類。固定していなければ null。 */
  lockKindOf(pieceId: number): LockKind | null;
  /** 固定中のピース id（昇順）。 */
  lockedIds(): number[];
  /** 配置をまるごと入れ替える（「散らし直す」）。'manual' の固定は解除し 'hint' は保つ。 */
  reset(placements: readonly Placement[]): void;
};

/**
 * ゲーム状態を作る。
 * 更新のたびに `isSolved` を呼ぶ（RULES.md 3.4「判定はピースを動かすたびに実行」）。
 * onChange は構築時には呼ばない。初回の描画は呼び出し側が placements() から行う。
 */
export function createGame(
  pieces: readonly Piece[],
  n: number,
  initial: readonly Placement[],
  onChange?: GameChangeListener,
): Game {
  const list: readonly Piece[] = pieces.slice();
  const known = new Set(list.map((piece): number => piece.id));
  /** 固定中のピース id → 固定の種類。配置とは別に持つ（固定しても配置は変わらない）。 */
  const locks = new Map<number, LockKind>();

  /** 未知のピース id は呼び出し側のバグなので例外にする。 */
  const assertKnown = (pieceId: number): void => {
    if (!known.has(pieceId)) throw new Error(`未知のピース id ${pieceId}`);
  };

  /** 配置の集合が全ピースをちょうど 1 回ずつ含むか確かめる。 */
  const validate = (placements: readonly Placement[]): Placement[] => {
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
      const placement = placementFor(pieceId);
      // 固定中のピースは動かせない。何も変わらないので onChange も呼ばない
      if (locks.has(pieceId)) return;
      apply(replacePlacement(current, movePlacement(placement, delta)));
    },
    rotate(pieceId: number, axis: Axis, dir: 1 | -1): void {
      const placement = placementFor(pieceId);
      if (locks.has(pieceId)) return;
      apply(replacePlacement(current, rotatePlacement(placement, axis, dir)));
    },
    place(pieceId: number, orientation: number, position: Vec3): void {
      assertKnown(pieceId);
      orientationMatrix(orientation); // 向き id が 0..23 かをここで確かめる
      if (locks.has(pieceId)) return;
      apply(replacePlacement(current, { pieceId, orientation, position }));
    },
    lock(pieceId: number, kind: LockKind): void {
      assertKnown(pieceId);
      locks.set(pieceId, kind);
      // 固定しても配置は変わらないので onChange は呼ばない（見た目の更新は呼び出し側の責務）
    },
    unlock(pieceId: number): void {
      assertKnown(pieceId);
      // 'hint' の固定は解除できない（ヒントで置いたピースは動かせないまま）
      if (locks.get(pieceId) === 'manual') locks.delete(pieceId);
      // 解除も配置を変えないので onChange は呼ばない
    },
    lockKindOf(pieceId: number): LockKind | null {
      return locks.get(pieceId) ?? null;
    },
    lockedIds(): number[] {
      return [...locks.keys()].sort((a, b): number => a - b);
    },
    reset(placements: readonly Placement[]): void {
      const next = validate(placements);
      // 散らし直しでは人の固定は解け、ヒントの固定は残る（残す配置は呼び出し側が keep で作る）
      for (const [pieceId, kind] of [...locks]) {
        if (kind === 'manual') locks.delete(pieceId);
      }
      apply(next);
    },
  };
}
