// セーブデータの形と localStorage との入出力（RULES.md 3.1 の難易度・3.6 の再現性を前提にする）。
// `src/ui/params.ts` と同じ立ち位置で、DOM にも Three.js にも依存しない純粋関数 + 薄い入出力だけを置く。
// Vitest を Node で走らせるため、モジュールの読み込み時に `window` / `localStorage` を参照しない
// （`localStorage` は呼び出し側から `SaveStorage` として渡す）。
//
// 壊れたデータ（別バージョン・手で書き換えられた JSON・型違い・範囲外）は例外を投げずに
// 既定値へフォールバックする（params.ts の不正値の扱いと同じ思想）。
//
// 解釈: 選択中のピースとカメラの位置は保存しない。再開時は「選択なし・カメラは初期位置」でよく、
// 保存しても復元の手間に見合わないため。
// 解釈: 盤面はピース形状ごとではなく「難易度 + シード + 配置 + 固定」で保存する。形は
// `generatePuzzle(n, m, seed)` で再現できる（RULES.md 3.6）ので保存量が小さく、生成規則が
// 変わったときには（ピース数の食い違いとして）検出できる。
// 解釈: 残りピース数は保存する。タイトル画面では盤面を再生成せずに「途中の盤面あり」を出したい。

import { MAX_SPACE_SIZE, MIN_PIECE_COUNT, MIN_SPACE_SIZE, maxPieces } from '../core/generate';
import { ORIENTATION_COUNT } from '../core/grid';
import type { LockKind } from '../core/game';
import type { Placement } from '../core/piece';
import { DEFAULT_ALLOW_ROTATION, DEFAULT_PIECE_COUNT, DEFAULT_SPACE_SIZE } from './difficulty';
import { MAX_SEED, MIN_SEED } from './params';

/** localStorage のキー。解釈: 形を変えたら読み捨てられるよう版を含める。 */
export const SAVE_KEY = 'cubelith.save.v1';
export const SAVE_VERSION = 1;

/** 難易度（RULES.md 3.1 の N / M / パズルの回転）。シードは難易度に含めない。 */
export type SavedDifficulty = {
  readonly n: number;
  readonly m: number;
  readonly allowRotation: boolean;
};

/** 固定中のピース 1 つ分。 */
export type SavedLock = { readonly pieceId: number; readonly kind: LockKind };

/** 中断した盤面。seed と難易度があれば puzzle は生成し直せるので、形そのものは保存しない。 */
export type SavedProgress = {
  readonly difficulty: SavedDifficulty;
  readonly seed: number;
  readonly placements: readonly Placement[];
  /** 固定中のピースだけ。id 昇順。 */
  readonly locks: readonly SavedLock[];
  /** タイトルに出す残りピース数（未確定のピース数）。復元せずに表示できるよう持っておく。 */
  readonly remaining: number;
};

/** クリア回数。合計と難易度ごと（キーは `difficultyKey`）。 */
export type SavedClears = {
  readonly total: number;
  readonly byDifficulty: Readonly<Record<string, number>>;
};

export type SaveData = {
  readonly version: number;
  /** 最後に選んだ難易度。 */
  readonly difficulty: SavedDifficulty;
  /** 中断した盤面。無ければ null。 */
  readonly progress: SavedProgress | null;
  /** クリア回数。合計と難易度ごと。 */
  readonly clears: SavedClears;
};

/** `localStorage` がそのまま満たす最小の形。テストでは Map で代用する。 */
export type SaveStorage = {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
  removeItem(key: string): void;
};

/** 固定の種類（core/game の LockKind）。JSON から読むので値の一覧をここに持つ。 */
const LOCK_KINDS: readonly string[] = ['manual', 'hint'];

/** 何も遊んでいない状態のセーブデータ。難易度は difficulty.ts の既定に合わせる。 */
export function defaultSaveData(): SaveData {
  return {
    version: SAVE_VERSION,
    difficulty: {
      n: DEFAULT_SPACE_SIZE,
      m: DEFAULT_PIECE_COUNT,
      allowRotation: DEFAULT_ALLOW_ROTATION,
    },
    progress: null,
    clears: { total: 0, byDifficulty: {} },
  };
}

/**
 * 難易度ごとの集計キー。例 `'3-4-0'`（パズルの回転ありは `1`）。
 * JSON のオブジェクトキーになるので、順序と区切りを固定した安定した文字列にする。
 */
export function difficultyKey(d: SavedDifficulty): string {
  return `${d.n}-${d.m}-${d.allowRotation ? 1 : 0}`;
}

/** `{}` のようなプレーンオブジェクトか（配列と null は除く）。 */
function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** [min, max] の整数か。NaN / Infinity / 小数 / 数値以外はすべて false。 */
function isIntInRange(value: unknown, min: number, max: number): value is number {
  return typeof value === 'number' && Number.isInteger(value) && value >= min && value <= max;
}

/** 0 以上の安全な整数か。 */
function isCount(value: unknown): value is number {
  return isIntInRange(value, 0, Number.MAX_SAFE_INTEGER);
}

/** x / y / z が有限な整数のボクセル座標。 */
function parsePosition(raw: unknown): Placement['position'] | null {
  if (!isRecord(raw)) return null;
  const { x, y, z } = raw;
  const limit = Number.MAX_SAFE_INTEGER;
  if (!isIntInRange(x, -limit, limit)) return null;
  if (!isIntInRange(y, -limit, limit)) return null;
  if (!isIntInRange(z, -limit, limit)) return null;
  return { x, y, z };
}

/** N / M / 回転を検証する。M の上限は N から決まる（core/generate の maxPieces）。 */
function parseDifficulty(raw: unknown): SavedDifficulty | null {
  if (!isRecord(raw)) return null;
  const { n, m, allowRotation } = raw;
  if (typeof allowRotation !== 'boolean') return null;
  if (!isIntInRange(n, MIN_SPACE_SIZE, MAX_SPACE_SIZE)) return null;
  if (!isIntInRange(m, MIN_PIECE_COUNT, maxPieces(n))) return null;
  return { n, m, allowRotation };
}

/** 1 個以上の配置。pieceId は 0 以上の整数で重複なし、orientation は 0..23。 */
function parsePlacements(raw: unknown): Placement[] | null {
  if (!Array.isArray(raw) || raw.length === 0) return null;
  const placements: Placement[] = [];
  const seen = new Set<number>();
  for (const item of raw as readonly unknown[]) {
    if (!isRecord(item)) return null;
    const { pieceId, orientation, position } = item;
    if (!isCount(pieceId) || seen.has(pieceId)) return null;
    if (!isIntInRange(orientation, 0, ORIENTATION_COUNT - 1)) return null;
    const parsed = parsePosition(position);
    if (parsed === null) return null;
    seen.add(pieceId);
    placements.push({ pieceId, orientation, position: parsed });
  }
  return placements;
}

/** 固定。pieceId は placements にある id、kind は 'manual' | 'hint'。id 昇順に並べ直す。 */
function parseLocks(raw: unknown, knownIds: ReadonlySet<number>): SavedLock[] | null {
  if (!Array.isArray(raw)) return null;
  const locks: SavedLock[] = [];
  const seen = new Set<number>();
  for (const item of raw as readonly unknown[]) {
    if (!isRecord(item)) return null;
    const { pieceId, kind } = item;
    if (!isCount(pieceId) || !knownIds.has(pieceId) || seen.has(pieceId)) return null;
    if (typeof kind !== 'string' || !LOCK_KINDS.includes(kind)) return null;
    seen.add(pieceId);
    locks.push({ pieceId, kind: kind as LockKind });
  }
  locks.sort((a, b): number => a.pieceId - b.pieceId);
  return locks;
}

/**
 * 中断した盤面。難易度・シード・配置・固定・残りピース数のどれかが壊れていれば null。
 * ピース数が難易度の M と食い違う場合も null にする（復元時に createGame が例外を投げるのを防ぐ）。
 */
function parseProgress(raw: unknown): SavedProgress | null {
  if (!isRecord(raw)) return null;
  const difficulty = parseDifficulty(raw.difficulty);
  if (difficulty === null) return null;
  if (!isIntInRange(raw.seed, MIN_SEED, MAX_SEED)) return null;
  const placements = parsePlacements(raw.placements);
  if (placements === null) return null;
  const locks = parseLocks(raw.locks, new Set(placements.map((p): number => p.pieceId)));
  if (locks === null) return null;
  if (!isIntInRange(raw.remaining, 0, placements.length)) return null;
  return { difficulty, seed: raw.seed, placements, locks, remaining: raw.remaining };
}

/**
 * クリア回数。解釈: 難易度ごとの値は 1 つ壊れていてもそのキーだけ落として読む
 * （回数の記録は遊びの進行に影響しないので、全体を捨てるより残すほうが損が小さい）。
 * 一方 total とオブジェクトそのものの形は必須にして、別物の JSON を受け入れないようにする。
 */
function parseClears(raw: unknown): SavedClears | null {
  if (!isRecord(raw)) return null;
  if (!isCount(raw.total)) return null;
  if (!isRecord(raw.byDifficulty)) return null;
  const byDifficulty: Record<string, number> = {};
  for (const [key, value] of Object.entries(raw.byDifficulty)) {
    if (isCount(value)) byDifficulty[key] = value;
  }
  return { total: raw.total, byDifficulty };
}

/**
 * 保存されていた文字列を読む。JSON として読めて version が一致し、各フィールドが型・範囲を
 * 満たすときだけ `SaveData` を返す。それ以外は null（呼び出し側は既定値へフォールバックする）。
 *
 * 例外: progress だけが「ピース数が M と食い違う」場合は progress を捨てて難易度と clears を生かす。
 */
export function parseSaveData(text: string | null | undefined): SaveData | null {
  if (text === null || text === undefined || text === '') return null;
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return null;
  }
  if (!isRecord(raw)) return null;
  if (raw.version !== SAVE_VERSION) return null;
  const difficulty = parseDifficulty(raw.difficulty);
  if (difficulty === null) return null;
  const clears = parseClears(raw.clears);
  if (clears === null) return null;

  let progress: SavedProgress | null = null;
  if (raw.progress !== null && raw.progress !== undefined) {
    progress = parseProgress(raw.progress);
    if (progress === null) return null;
    // 生成規則が変わった / 手で書き換えられた場合。難易度と clears は生かして盤面だけ捨てる
    if (progress.placements.length !== progress.difficulty.m) progress = null;
  }

  return { version: SAVE_VERSION, difficulty, progress, clears };
}

/** 保存する文字列。読みに使うフィールド以外は書かない。 */
export function serializeSaveData(data: SaveData): string {
  const progress = data.progress;
  return JSON.stringify({
    version: SAVE_VERSION,
    difficulty: {
      n: data.difficulty.n,
      m: data.difficulty.m,
      allowRotation: data.difficulty.allowRotation,
    },
    progress:
      progress === null
        ? null
        : {
            difficulty: {
              n: progress.difficulty.n,
              m: progress.difficulty.m,
              allowRotation: progress.difficulty.allowRotation,
            },
            seed: progress.seed,
            placements: progress.placements.map((p): Placement => ({
              pieceId: p.pieceId,
              orientation: p.orientation,
              position: { x: p.position.x, y: p.position.y, z: p.position.z },
            })),
            locks: [...progress.locks]
              .sort((a, b): number => a.pieceId - b.pieceId)
              .map((lock): SavedLock => ({ pieceId: lock.pieceId, kind: lock.kind })),
            remaining: progress.remaining,
          },
    clears: { total: data.clears.total, byDifficulty: { ...data.clears.byDifficulty } },
  });
}

/** 保存されたデータを読む。storage が無い / 読めない / 壊れているときは既定値。 */
export function loadSave(storage: SaveStorage | null | undefined): SaveData {
  if (storage === null || storage === undefined) return defaultSaveData();
  let text: string | null = null;
  try {
    text = storage.getItem(SAVE_KEY);
  } catch {
    // プライベートモードなどで getItem 自体が投げることがある
    return defaultSaveData();
  }
  return parseSaveData(text) ?? defaultSaveData();
}

/** 保存する。storage が無ければ何もしない。容量超過などの例外は握りつぶす（遊びは続けられる）。 */
export function storeSave(storage: SaveStorage | null | undefined, data: SaveData): void {
  if (storage === null || storage === undefined) return;
  try {
    storage.setItem(SAVE_KEY, serializeSaveData(data));
  } catch {
    // 容量超過 / プライベートモード。保存できなくても遊びは続けられるので無視する
  }
}

/** 選んだ難易度を差し替える。 */
export function withDifficulty(data: SaveData, difficulty: SavedDifficulty): SaveData {
  return { ...data, difficulty };
}

/** 中断した盤面を記録する。「最後に選んだ難易度」も盤面のものへ揃える。 */
export function withProgress(data: SaveData, progress: SavedProgress): SaveData {
  return { ...data, difficulty: progress.difficulty, progress };
}

/** 中断した盤面を忘れる（「もう一度」やタイトルから新しい盤面を始めたとき）。 */
export function withoutProgress(data: SaveData): SaveData {
  return { ...data, progress: null };
}

/** その難易度のクリア回数（記録が無ければ 0）。 */
export function clearCountOf(data: SaveData, difficulty: SavedDifficulty): number {
  return data.clears.byDifficulty[difficultyKey(difficulty)] ?? 0;
}

/** クリアを 1 回記録する。クリアした盤面は「途中」ではないので progress は捨てる。 */
export function withClearRecorded(data: SaveData, difficulty: SavedDifficulty): SaveData {
  const key = difficultyKey(difficulty);
  return {
    ...data,
    difficulty,
    progress: null,
    clears: {
      total: data.clears.total + 1,
      byDifficulty: { ...data.clears.byDifficulty, [key]: clearCountOf(data, difficulty) + 1 },
    },
  };
}
