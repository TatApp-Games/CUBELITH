// セーブデータの形と localStorage 入出力（src/ui/save.ts）。
// 壊れたデータで例外を投げず既定値へ落ちること、更新ヘルパが元を書き換えないことを確かめる。

import { describe, expect, it } from 'vitest';
import {
  clearCountOf,
  defaultSaveData,
  difficultyKey,
  loadSave,
  parseSaveData,
  progressFitsPieces,
  SAVE_KEY,
  SAVE_VERSION,
  serializeSaveData,
  storeSave,
  withClearRecorded,
  withDifficulty,
  withoutProgress,
  withProgress,
  type SaveData,
  type SavedDifficulty,
  type SaveStorage,
  type SavedProgress,
} from '../src/ui/save';

/** N=3 / M=4 / 回転なし。difficulty.ts の既定と同じ。 */
const EASY: SavedDifficulty = { n: 3, m: 4, allowRotation: false };
/** 回転あり。difficultyKey が EASY と別になる。 */
const EASY_ROTATED: SavedDifficulty = { n: 3, m: 4, allowRotation: true };
const HARD: SavedDifficulty = { n: 5, m: 8, allowRotation: true };

/** m 個のピースを縦に並べただけの配置。 */
function placementsOf(m: number): { pieceId: number; orientation: number; position: { x: number; y: number; z: number } }[] {
  const placements = [];
  for (let i = 0; i < m; i += 1) {
    placements.push({ pieceId: i, orientation: i % 24, position: { x: i, y: -(i + 1), z: 0 } });
  }
  return placements;
}

/** 中断した盤面のサンプル。 */
function progressOf(difficulty: SavedDifficulty): SavedProgress {
  return {
    difficulty,
    seed: 12345,
    placements: placementsOf(difficulty.m),
    locks: [{ pieceId: 1, kind: 'manual' }],
    remaining: difficulty.m - 1,
  };
}

/** Map で作った localStorage 代用。 */
function memoryStorage(initial?: ReadonlyMap<string, string>): SaveStorage & { map: Map<string, string> } {
  const map = new Map<string, string>(initial);
  return {
    map,
    getItem(key: string): string | null {
      return map.get(key) ?? null;
    },
    setItem(key: string, value: string): void {
      map.set(key, value);
    },
    removeItem(key: string): void {
      map.delete(key);
    },
  };
}

/** 何をしても投げるストレージ（プライベートモード・容量超過を模す）。 */
function throwingStorage(): SaveStorage {
  return {
    getItem(): string | null {
      throw new Error('getItem が使えない');
    },
    setItem(): void {
      throw new Error('容量超過');
    },
    removeItem(): void {
      throw new Error('removeItem が使えない');
    },
  };
}

/** 既定値 + 中断した盤面 + クリア記録の入った、正しいセーブデータ。 */
function fullSaveData(): SaveData {
  return withClearRecorded(withProgress(defaultSaveData(), progressOf(EASY)), HARD);
}

describe('defaultSaveData', () => {
  it('難易度は difficulty.ts の既定・progress は null・clears は 0', () => {
    const data = defaultSaveData();
    expect(data.version).toBe(SAVE_VERSION);
    expect(data.difficulty).toEqual(EASY);
    expect(data.progress).toBeNull();
    expect(data.clears).toEqual({ total: 0, byDifficulty: {} });
  });
});

describe('difficultyKey', () => {
  it('N / M / 回転の有無で別のキーになる', () => {
    expect(difficultyKey(EASY)).toBe('3-4-0');
    expect(difficultyKey(EASY_ROTATED)).toBe('3-4-1');
    expect(difficultyKey(EASY)).not.toBe(difficultyKey(EASY_ROTATED));
    expect(difficultyKey(HARD)).toBe('5-8-1');
  });
});

describe('serializeSaveData / parseSaveData', () => {
  it('既定値は往復して同じ内容になる', () => {
    const data = defaultSaveData();
    expect(parseSaveData(serializeSaveData(data))).toEqual(data);
  });

  it('盤面とクリア記録が入っていても往復して同じ内容になる', () => {
    const data = fullSaveData();
    const restored = parseSaveData(serializeSaveData(withProgress(data, progressOf(HARD))));
    expect(restored).toEqual(withProgress(data, progressOf(HARD)));
  });

  it('読みに使うフィールド以外は書かない', () => {
    const data = { ...defaultSaveData(), extra: 'ゴミ' } as unknown as SaveData;
    expect(Object.keys(JSON.parse(serializeSaveData(data)) as object).sort()).toEqual([
      'clears',
      'difficulty',
      'progress',
      'version',
    ]);
  });
});

describe('parseSaveData の不正入力', () => {
  it('JSON として読めない / 空 / null は null', () => {
    for (const text of [null, undefined, '', '{', 'ゴミ']) {
      expect(parseSaveData(text), String(text)).toBeNull();
    }
  });

  it('JSON だがオブジェクトでないものは null', () => {
    for (const text of ['[]', '[1,2,3]', '3', '"文字列"', 'null', 'true']) {
      expect(parseSaveData(text), text).toBeNull();
    }
  });

  it('version が違えば null', () => {
    const raw = JSON.parse(serializeSaveData(defaultSaveData())) as Record<string, unknown>;
    for (const version of [0, 2, '1', null, undefined]) {
      expect(parseSaveData(JSON.stringify({ ...raw, version })), String(version)).toBeNull();
    }
  });

  it('難易度が範囲外 / 型違いなら null', () => {
    const broken: unknown[] = [
      { n: 2, m: 4, allowRotation: false },
      { n: 8, m: 4, allowRotation: false },
      { n: 3.5, m: 4, allowRotation: false },
      { n: 3, m: 1, allowRotation: false },
      // maxPieces(3) = 7 を超える M
      { n: 3, m: 8, allowRotation: false },
      { n: 3, m: 4, allowRotation: 'false' },
      { n: '3', m: 4, allowRotation: false },
      null,
      [3, 4, false],
    ];
    for (const difficulty of broken) {
      const text = JSON.stringify({ ...defaultSaveData(), difficulty });
      expect(parseSaveData(text), JSON.stringify(difficulty)).toBeNull();
    }
  });

  it('clears が壊れていれば null。難易度ごとの壊れた値はそのキーだけ落として読む', () => {
    const base = defaultSaveData();
    for (const clears of [null, 3, { total: -1, byDifficulty: {} }, { total: 1.5, byDifficulty: {} }, { total: 1 }]) {
      expect(parseSaveData(JSON.stringify({ ...base, clears })), JSON.stringify(clears)).toBeNull();
    }
    const mixed = JSON.stringify({
      ...base,
      clears: { total: 2, byDifficulty: { '3-4-0': 2, '5-8-1': -1, '7-27-0': 'ゴミ' } },
    });
    expect(parseSaveData(mixed)?.clears).toEqual({ total: 2, byDifficulty: { '3-4-0': 2 } });
  });

  it('progress の中身が壊れていれば null', () => {
    const base = withProgress(defaultSaveData(), progressOf(EASY));
    const broken: readonly Record<string, unknown>[] = [
      // seed が範囲外
      { seed: -1 },
      { seed: 0x100000000 },
      { seed: 1.5 },
      // orientation が 0..23 の外
      { placements: [...placementsOf(3), { pieceId: 3, orientation: 24, position: { x: 0, y: 0, z: 0 } }] },
      // pieceId の重複
      { placements: [...placementsOf(3), { pieceId: 0, orientation: 0, position: { x: 0, y: 0, z: 0 } }] },
      // position が小数
      { placements: [...placementsOf(3), { pieceId: 3, orientation: 0, position: { x: 0.5, y: 0, z: 0 } }] },
      // position の要素が足りない
      { placements: [...placementsOf(3), { pieceId: 3, orientation: 0, position: { x: 0, y: 0 } }] },
      // placements が空 / 配列でない
      { placements: [] },
      { placements: {} },
      // locks の id が placements に無い / kind が知らない値
      { locks: [{ pieceId: 99, kind: 'manual' }] },
      { locks: [{ pieceId: 0, kind: 'auto' }] },
      { locks: {} },
      // remaining が範囲外
      { remaining: -1 },
      { remaining: 5 },
      { remaining: 1.5 },
      // 難易度が壊れている
      { difficulty: { n: 3, m: 0, allowRotation: false } },
    ];
    for (const patch of broken) {
      const text = JSON.stringify({ ...base, progress: { ...progressOf(EASY), ...patch } });
      expect(parseSaveData(text), JSON.stringify(patch)).toBeNull();
    }
  });

  it('progress が無いものは progress: null として読む', () => {
    const base = defaultSaveData();
    for (const progress of [null, undefined]) {
      const parsed = parseSaveData(JSON.stringify({ ...base, progress }));
      expect(parsed?.progress, String(progress)).toBeNull();
    }
  });

  it('ピース数が M と食い違う progress は落とし、難易度と clears は残す', () => {
    const data = withProgress(withClearRecorded(defaultSaveData(), HARD), progressOf(HARD));
    const raw = JSON.parse(serializeSaveData(data)) as Record<string, unknown>;
    const progress = { ...progressOf(HARD), placements: placementsOf(HARD.m - 1), remaining: 3 };
    const parsed = parseSaveData(JSON.stringify({ ...raw, progress }));
    expect(parsed).not.toBeNull();
    expect(parsed?.progress).toBeNull();
    expect(parsed?.difficulty).toEqual(HARD);
    expect(parsed?.clears).toEqual({ total: 1, byDifficulty: { [difficultyKey(HARD)]: 1 } });
  });
});

describe('更新ヘルパ', () => {
  it('withDifficulty は難易度だけを差し替え、元を書き換えない', () => {
    const base = fullSaveData();
    const snapshot = JSON.parse(serializeSaveData(base)) as unknown;
    const next = withDifficulty(base, HARD);
    expect(next.difficulty).toEqual(HARD);
    expect(next.clears).toEqual(base.clears);
    expect(JSON.parse(serializeSaveData(base))).toEqual(snapshot);
  });

  it('withProgress は盤面と難易度を同時に更新する', () => {
    const next = withProgress(defaultSaveData(), progressOf(HARD));
    expect(next.progress).toEqual(progressOf(HARD));
    expect(next.difficulty).toEqual(HARD);
  });

  it('withoutProgress は盤面だけを忘れる', () => {
    const base = withProgress(defaultSaveData(), progressOf(HARD));
    const next = withoutProgress(base);
    expect(next.progress).toBeNull();
    expect(next.difficulty).toEqual(HARD);
    expect(base.progress).not.toBeNull();
  });

  it('withClearRecorded は合計と当該難易度を 1 ずつ増やし、progress を捨てる', () => {
    const base = withProgress(defaultSaveData(), progressOf(EASY));
    const once = withClearRecorded(base, EASY);
    expect(once.clears.total).toBe(1);
    expect(clearCountOf(once, EASY)).toBe(1);
    expect(once.progress).toBeNull();
    expect(base.progress).not.toBeNull();

    const twice = withClearRecorded(once, EASY);
    expect(twice.clears.total).toBe(2);
    expect(clearCountOf(twice, EASY)).toBe(2);

    const mixed = withClearRecorded(withClearRecorded(twice, HARD), EASY_ROTATED);
    expect(mixed.clears.total).toBe(4);
    expect(clearCountOf(mixed, EASY)).toBe(2);
    expect(clearCountOf(mixed, HARD)).toBe(1);
    expect(clearCountOf(mixed, EASY_ROTATED)).toBe(1);
  });

  it('別の難易度を挟んでも他の難易度の回数は増えない', () => {
    let data = defaultSaveData();
    data = withClearRecorded(data, EASY);
    data = withClearRecorded(data, HARD);
    data = withClearRecorded(data, EASY);
    expect(data.clears.total).toBe(3);
    expect(clearCountOf(data, EASY)).toBe(2);
    expect(clearCountOf(data, HARD)).toBe(1);
  });

  it('clearCountOf は記録が無ければ 0', () => {
    expect(clearCountOf(defaultSaveData(), EASY)).toBe(0);
    expect(clearCountOf(fullSaveData(), EASY_ROTATED)).toBe(0);
  });
});

describe('progressFitsPieces', () => {
  it('ピース id が過不足なく揃っていれば true（並び順は問わない）', () => {
    const progress = progressOf(EASY);
    expect(progressFitsPieces(progress, [0, 1, 2, 3])).toBe(true);
    expect(progressFitsPieces(progress, [3, 1, 0, 2])).toBe(true);
  });

  it('ピースの数が違えば false', () => {
    const progress = progressOf(EASY);
    expect(progressFitsPieces(progress, [0, 1, 2])).toBe(false);
    expect(progressFitsPieces(progress, [0, 1, 2, 3, 4])).toBe(false);
  });

  it('数が同じでも id が食い違えば false', () => {
    expect(progressFitsPieces(progressOf(EASY), [0, 1, 2, 9])).toBe(false);
  });

  it('保存された配置に重複した id があれば false', () => {
    const progress: SavedProgress = {
      ...progressOf(EASY),
      placements: [...placementsOf(3), { pieceId: 0, orientation: 0, position: { x: 0, y: 0, z: 0 } }],
    };
    expect(progressFitsPieces(progress, [0, 1, 2, 3])).toBe(false);
  });
});

describe('loadSave / storeSave', () => {
  it('ダミーストレージで往復する', () => {
    const storage = memoryStorage();
    const data = fullSaveData();
    storeSave(storage, data);
    expect(storage.map.has(SAVE_KEY)).toBe(true);
    expect(loadSave(storage)).toEqual(data);
  });

  it('何も入っていなければ既定値', () => {
    expect(loadSave(memoryStorage())).toEqual(defaultSaveData());
  });

  it('壊れたデータが入っていても既定値を返す', () => {
    const storage = memoryStorage(new Map([[SAVE_KEY, '{壊れた']]));
    expect(loadSave(storage)).toEqual(defaultSaveData());
  });

  it('getItem / setItem が投げても例外が外へ出ず、既定値を返す', () => {
    const storage = throwingStorage();
    expect(() => storeSave(storage, fullSaveData())).not.toThrow();
    expect(loadSave(storage)).toEqual(defaultSaveData());
  });

  it('storage が null / undefined でも動く', () => {
    for (const storage of [null, undefined]) {
      expect(() => storeSave(storage, fullSaveData())).not.toThrow();
      expect(loadSave(storage)).toEqual(defaultSaveData());
    }
  });
});
