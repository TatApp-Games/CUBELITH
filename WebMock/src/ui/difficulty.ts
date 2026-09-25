// 難易度選択の選択肢（RULES.md 3.1）。N は 3〜7、M は N ごとのプリセット。
// 有効範囲の判断は core（maxPieces）に任せ、ここは「どう刻んで見せるか」だけを決める。

import { MAX_SPACE_SIZE, MIN_PIECE_COUNT, MIN_SPACE_SIZE, maxPieces } from '../core/generate';

/** RULES.md 3.1 の既定（N=3 / M=4）。 */
export const DEFAULT_SPACE_SIZE = 3;
export const DEFAULT_PIECE_COUNT = 4;

/**
 * パズルの回転（要望による難易度の追加項目）の既定。
 * 既定は「なし」= ピースの向きを一切変えない遊び方（散らしの向きが恒等で、回転操作の UI も出ない）。
 */
export const DEFAULT_ALLOW_ROTATION = false;

/** M のプリセットの段数（RULES.md 3.1）。 */
const PRESET_STEPS = 5;

/** 選べる N の一覧（3..7）。 */
export function spaceSizes(): number[] {
  const sizes: number[] = [];
  for (let n = MIN_SPACE_SIZE; n <= MAX_SPACE_SIZE; n += 1) sizes.push(n);
  return sizes;
}

/**
 * N に対する M のプリセット（RULES.md 3.1）。
 *
 * N から maxPieces(n) までを PRESET_STEPS 段に等分する。刻みは N−2 になり、
 * N=3 → 3/4/5/6/7、N=4 → 4/6/8/10/12、N=5 → 5/8/11/14/17、N=6 → 6/10/14/18/22、N=7 → 7/12/17/22/27。
 * 既定の M=4 は N=3 のプリセットに含まれる。
 */
export function piecePresets(n: number): number[] {
  const limit = maxPieces(n);
  const presets: number[] = [];
  for (let i = 0; i < PRESET_STEPS; i += 1) {
    const t = i / (PRESET_STEPS - 1);
    const value = Math.round(n + t * (limit - n));
    if (!presets.includes(value)) presets.push(value);
  }
  return presets;
}

/** presets のうち m に最も近いもの。N を切り替えたときに選択を引き継ぐのに使う。 */
export function nearestPreset(presets: readonly number[], m: number): number {
  let best = presets[0] ?? MIN_PIECE_COUNT;
  for (const value of presets) {
    if (Math.abs(value - m) < Math.abs(best - m)) best = value;
  }
  return best;
}

/** 新しい乱数シード（RULES.md 3.1）。「もう一度」やタイトルへ戻るたびに引き直す。 */
export function randomSeed(): number {
  return Math.floor(Math.random() * 0x7fffffff);
}
