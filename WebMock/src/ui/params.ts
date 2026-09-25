// URL クエリの解釈（RULES.md 3.1 のシードの指定を、Web では `?seed=` で行う。SPEC.md 7 章）。
// Three.js にも DOM にも依存しない純粋な関数だけを置く。呼び出し側（main.ts）が
// `window.location.search` を渡し、結果を難易度・描画品質に反映する。
//
// 不正な値（数値でない・空・範囲外）は「無かったこと」にして既定動作へフォールバックする
// （RULES.md 3.1「指定が無ければ乱数」）。例外は投げない。

/** シードとして受け付ける範囲。mulberry32（core/rng）が符号なし 32 bit を使うのに合わせる。 */
export const MIN_SEED = 0;
export const MAX_SEED = 0xffffffff;

/** URL クエリから読み取った指定。値が無い / 不正なものは undefined。 */
export type AppParams = {
  /** 空間サイズ N（`?n=`）。範囲の丸めは呼び出し側で行う。 */
  readonly n: number | undefined;
  /** 分割数 M（`?m=`）。 */
  readonly m: number | undefined;
  /** 生成シード（`?seed=`）。 */
  readonly seed: number | undefined;
  /** 軽量描画モード（`?lite=`）。未指定なら undefined = 端末判定に任せる。 */
  readonly lite: boolean | undefined;
  /** 簡易 FPS 表示（`?fps=1`）。 */
  readonly fps: boolean;
};

/** 10 進整数だけを受け付ける。`12abc` や `1.5` のような「途中まで数字」は不正扱いにする。 */
const INTEGER_PATTERN = /^[+-]?\d+$/;

const TRUE_WORDS: readonly string[] = ['1', 'true', 'on', 'yes'];
const FALSE_WORDS: readonly string[] = ['0', 'false', 'off', 'no'];

/**
 * クエリ値を整数として読む。null / 空文字 / 整数以外は undefined。
 * `Number.parseInt` と違い、末尾にゴミが付いた値は受け付けない。
 */
export function parseIntParam(raw: string | null | undefined): number | undefined {
  if (raw === null || raw === undefined) return undefined;
  const text = raw.trim();
  if (!INTEGER_PATTERN.test(text)) return undefined;
  const value = Number(text);
  return Number.isSafeInteger(value) ? value : undefined;
}

/** クエリ値を真偽として読む。`1/true/on/yes` と `0/false/off/no` 以外は undefined。 */
export function parseBoolParam(raw: string | null | undefined): boolean | undefined {
  if (raw === null || raw === undefined) return undefined;
  const text = raw.trim().toLowerCase();
  if (TRUE_WORDS.includes(text)) return true;
  if (FALSE_WORDS.includes(text)) return false;
  return undefined;
}

/** value を [min, max] に丸める。undefined ならそのまま undefined。 */
export function clampInt(
  value: number | undefined,
  min: number,
  max: number,
): number | undefined {
  if (value === undefined) return undefined;
  return Math.min(Math.max(Math.round(value), min), max);
}

/**
 * `?seed=` を読む。範囲外（負数や 2^32 以上）は丸めずに捨てる。
 *
 * 解釈: 仕様（RULES.md 3.1 / SPEC.md 7 章）は「シードを URL クエリで指定できる」としか書いていない。範囲外を丸めると
 * `?seed=-1` と `?seed=0` が同じ盤面になり「指定した値と盤面の対応」が壊れるので、
 * 丸めではなく無視（＝乱数へフォールバック）にする。
 */
export function parseSeedParam(raw: string | null | undefined): number | undefined {
  const value = parseIntParam(raw);
  if (value === undefined) return undefined;
  return value >= MIN_SEED && value <= MAX_SEED ? value : undefined;
}

/** `window.location.search` 相当の文字列から指定を読む。 */
export function readAppParams(search: string): AppParams {
  const params = new URLSearchParams(search);
  return {
    n: parseIntParam(params.get('n')),
    m: parseIntParam(params.get('m')),
    seed: parseSeedParam(params.get('seed')),
    lite: parseBoolParam(params.get('lite')),
    fps: parseBoolParam(params.get('fps')) ?? false,
  };
}

/**
 * search の n / m / seed を差し替えた新しいクエリ文字列（先頭に `?` 付き。空なら空文字）。
 * `?lite=` `?fps=` のような他の指定は残す。`history.replaceState` に渡して、
 * 遊んでいる盤面の seed が常に URL に出ている状態にするのに使う。
 */
export function withSettings(
  search: string,
  settings: { readonly n: number; readonly m: number; readonly seed: number },
): string {
  const params = new URLSearchParams(search);
  params.set('n', String(settings.n));
  params.set('m', String(settings.m));
  params.set('seed', String(settings.seed));
  const text = params.toString();
  return text === '' ? '' : `?${text}`;
}
