// 難易度選択の選択肢（SPEC.md 3.1）。N は 3〜7、M は N ごとのプリセット。
// 有効範囲の判断は core（maxPieces）に任せ、ここは「どう刻んで見せるか」だけを決める。

import { MAX_SPACE_SIZE, MIN_PIECE_COUNT, MIN_SPACE_SIZE, maxPieces } from '../core/generate';

/** SPEC.md 3.1 の既定（N=3 / M=4）。 */
export const DEFAULT_SPACE_SIZE = 3;
export const DEFAULT_PIECE_COUNT = 4;

/** M のプリセットの段数（SPEC.md 3.1「UI では 4〜6 段階のプリセットでよい」）。 */
const PRESET_STEPS = 5;

/** 選べる N の一覧（3..7）。 */
export function spaceSizes(): number[] {
  const sizes: number[] = [];
  for (let n = MIN_SPACE_SIZE; n <= MAX_SPACE_SIZE; n += 1) sizes.push(n);
  return sizes;
}

/**
 * N に対する M のプリセット。
 *
 * 解釈: SPEC.md 3.1 は段数（4〜6）しか決めていないので、有効範囲 2..maxPieces(n) を
 * PRESET_STEPS 段に等分して作る。丸めで重複した値は落とすので段数は 4〜5 になる。
 * N=3 では 2 / 3 / 4 / 5 / 6 となり、既定の M=4 が必ず含まれる。
 */
export function piecePresets(n: number): number[] {
  const limit = maxPieces(n);
  const presets: number[] = [];
  for (let i = 0; i < PRESET_STEPS; i += 1) {
    const t = i / (PRESET_STEPS - 1);
    const value = Math.round(MIN_PIECE_COUNT + t * (limit - MIN_PIECE_COUNT));
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

/** 新しい乱数シード（SPEC.md 3.1）。「もう一度」やタイトルへ戻るたびに引き直す。 */
export function randomSeed(): number {
  return Math.floor(Math.random() * 0x7fffffff);
}
