// スナップの補間移動（SPEC.md 3.5）。
// 論理上の Placement は整数座標のまま先に確定させ、見た目だけを 100〜150 ms かけて追いつかせる。
// こうするとクリア判定（src/core）が中間位置の影響を受けない。

import type { Vec3 } from '../core/grid';
import type { ViewOffset } from './pieces';

/** 補間にかける時間（ms）。SPEC.md 3.5 の 100〜150 ms の中央付近。 */
export const SNAP_DURATION_MS = 130;

/** 表示上のずれの配り先。null は「ずれ無し（論理位置そのまま）」。 */
export type OffsetSink = (pieceId: number, offset: ViewOffset | null) => void;

export type SnapMotion = {
  /**
   * 表示位置を offset だけずらした状態から 0 へ補間する。
   * offset には「移動前の位置 - 移動後の位置」を渡す（見た目が移動前から始まる）。
   * 同じピースに新しい補間が来たら前の補間は打ち切る。
   */
  start(pieceId: number, offset: Vec3, nowMs?: number): void;
  /** 毎フレーム呼ぶ。補間中のピースの表示位置を進める。 */
  update(nowMs?: number): void;
  /** 補間を打ち切り、表示を論理位置に戻す。 */
  cancel(pieceId: number): void;
  /** 補間中のピースがあるか。 */
  running(): boolean;
  /** すべての補間を打ち切る。 */
  dispose(): void;
};

/** 終わり際が緩む easing。吸い付きが「シュッ」と減速して止まる。 */
function easeOutCubic(t: number): number {
  return 1 - (1 - t) ** 3;
}

/** 進行中の 1 本の補間。 */
type Tween = { readonly offset: Vec3; readonly startMs: number };

/**
 * スナップの補間を束ねる。時間は呼び出し側から渡せるようにしてあり、既定は performance.now()。
 * setOffset には PieceViews.setOffset を渡す想定。
 */
export function createSnapMotion(
  setOffset: OffsetSink,
  durationMs: number = SNAP_DURATION_MS,
): SnapMotion {
  const tweens = new Map<number, Tween>();

  const clear = (pieceId: number): void => {
    if (!tweens.delete(pieceId)) return;
    setOffset(pieceId, null);
  };

  return {
    start(pieceId, offset, nowMs = performance.now()): void {
      if (offset.x === 0 && offset.y === 0 && offset.z === 0) {
        clear(pieceId);
        return;
      }
      // 前の補間はここで捨てられる（同じピースに 2 本走らせない）
      tweens.set(pieceId, { offset, startMs: nowMs });
      setOffset(pieceId, offset);
    },

    update(nowMs = performance.now()): void {
      if (tweens.size === 0) return;
      for (const [pieceId, tween] of [...tweens]) {
        const t = durationMs > 0 ? (nowMs - tween.startMs) / durationMs : 1;
        if (t >= 1) {
          clear(pieceId);
          continue;
        }
        const remaining = 1 - easeOutCubic(Math.max(t, 0));
        setOffset(pieceId, {
          x: tween.offset.x * remaining,
          y: tween.offset.y * remaining,
          z: tween.offset.z * remaining,
        });
      }
    },

    cancel(pieceId): void {
      clear(pieceId);
    },

    running(): boolean {
      return tweens.size > 0;
    },

    dispose(): void {
      for (const pieceId of [...tweens.keys()]) clear(pieceId);
    },
  };
}
