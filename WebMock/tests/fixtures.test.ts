// UE 版と照合するデータ（golden fixtures）の一致テスト（SPEC_UE.md 7.3。JSON の形は ../Docs/FIXTURES.md）。
//
// 狙いは「`src/core` の生成結果が変わったらここで落ちて気付ける」こと（RULES.md 3.6）。
// 期待値をこのファイルに書き下さず、書き出しと同じ `buildFixtureFiles()` で作り直した内容を、
// コミット済みの JSON と比べる（照合データの中身の定義は `scripts/fixtures/build.ts` だけに置く）。
//
// ここが落ちたときに Fixtures/ を書き直して通すのは誤り。まずこのテストと `src/core` の側を疑う。
// 書き直すのは RULES.md の変更履歴で「生成への影響」が「あり」の R を入れたときだけ
// （そのとき `npm run export:fixtures` をやり直す。../Docs/FIXTURES.md）。

import { readdirSync, readFileSync } from 'node:fs';

import { describe, expect, it } from 'vitest';

import {
  buildFixtureFiles,
  FORMAT_VERSION,
  HINT_COUNT,
  INDEX_FILE,
  PUZZLE_SEEDS,
  RNG_SEEDS,
  RNG_STREAM_LENGTH,
  stringifyFixture,
  type FixtureFile,
} from '../scripts/fixtures/build';
import { MAX_SPACE_SIZE, MIN_SPACE_SIZE } from '../src/core/generate';
import { IDENTITY_ORIENTATION, ORIENTATION_COUNT } from '../src/core/grid';
import { piecePresets } from '../src/ui/difficulty';

/** 照合データの置き場所。`process.cwd()` に依らないよう、このファイルの位置から解決する。 */
const FIXTURES_DIR = new URL('../../Source/CUBELITHCore/Private/Tests/Fixtures/', import.meta.url);

/** N・M の組み合わせの数（RULES.md 3.1 の 5 段 × N の 5 通り）。 */
const EXPECTED_COMBINATION_COUNT = 25;

/** ファイルの総数。目録・向きの表・乱数の列に、N・M の 25 通りを足した 28。 */
const EXPECTED_FILE_COUNT = EXPECTED_COMBINATION_COUNT + 3;

/** 1 ケースの中で「配置の配列」になっているキー。どれも M 件で id が 0..M-1 を 1 回ずつ持つ。 */
const PLACEMENT_KEYS = ['solution', 'scatter', 'reshuffleWithoutHints', 'reshuffleWithHints'] as const;

// ---- 読み取ったデータの型（build.ts の出力の形。../Docs/FIXTURES.md） ----

type JsonVec3 = readonly [number, number, number];

type JsonPlacement = {
  readonly pieceId: number;
  readonly orientation: number;
  readonly position: JsonVec3;
};

type JsonPiece = { readonly id: number; readonly voxels: readonly JsonVec3[] };

type JsonCase = {
  readonly seed: number;
  readonly allowRotation: boolean;
  readonly pieces: readonly JsonPiece[];
  readonly solution: readonly JsonPlacement[];
  readonly scatter: readonly JsonPlacement[];
  readonly reshuffleWithoutHints: readonly JsonPlacement[];
  readonly hintPieceIds: readonly number[];
  readonly reshuffleWithHints: readonly JsonPlacement[];
};

type JsonPuzzleFile = { readonly n: number; readonly m: number; readonly cases: readonly JsonCase[] };

type JsonCombination = { readonly n: number; readonly m: number; readonly file: string };

type JsonIndex = {
  readonly formatVersion: number;
  readonly orientationCount: number;
  readonly puzzleSeeds: readonly number[];
  readonly rngSeeds: readonly number[];
  readonly rngStreamLength: number;
  readonly hintCount: number;
  readonly files: readonly string[];
  readonly combinations: readonly JsonCombination[];
};

// ---- 小物 ----

/** 文字列の昇順比較。ロケールに依らない素の比較（build.ts の並べ方と揃える）。 */
function comparePath(a: string, b: string): number {
  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

function compareNumber(a: number, b: number): number {
  return a - b;
}

/** 改行を LF に揃える。git の設定で CRLF に変換されていても比べられるようにする。 */
function normalizeNewlines(text: string): string {
  return text.replace(/\r\n/g, '\n');
}

function readFixtureText(name: string): string {
  return normalizeNewlines(readFileSync(new URL(name, FIXTURES_DIR), 'utf8'));
}

function readFixtureJson(name: string): unknown {
  return JSON.parse(readFixtureText(name));
}

/** 0..count-1 の配列。ピース id の期待値に使う。 */
function rangeIds(count: number): number[] {
  const ids: number[] = [];
  for (let i = 0; i < count; i += 1) ids.push(i);
  return ids;
}

/** 書き出す N・M の組み合わせ（build.ts と同じ作り方。RULES.md 3.1 の表を書き写さない）。 */
function expectedCombinations(): JsonCombination[] {
  const combinations: JsonCombination[] = [];
  for (let n = MIN_SPACE_SIZE; n <= MAX_SPACE_SIZE; n += 1) {
    for (const m of [...piecePresets(n)].sort(compareNumber)) {
      combinations.push({ n, m, file: `puzzle_n${n}_m${m}.json` });
    }
  }
  return combinations;
}

function placementOf(placements: readonly JsonPlacement[], pieceId: number): JsonPlacement | undefined {
  return placements.find((placement): boolean => placement.pieceId === pieceId);
}

// ---- 比べる対象 ----

// 作り直しは 25 通り × 6 ケースの生成を回すので、ファイルごとに呼ばず 1 回だけにする
const builtFiles: readonly FixtureFile[] = buildFixtureFiles();
const builtPaths: readonly string[] = builtFiles.map((file): string => file.path);
const committedNames: readonly string[] = readdirSync(FIXTURES_DIR)
  .filter((name): boolean => name.endsWith('.json'))
  .sort(comparePath);

describe('照合データのファイルの集合', () => {
  it(`コミット済みの *.json が ${EXPECTED_FILE_COUNT} ファイルある`, () => {
    expect(committedNames).toHaveLength(EXPECTED_FILE_COUNT);
  });

  it('コミット済みの *.json と、作り直したファイル名の集合が完全に一致する', () => {
    // 書き出しは古いファイルを消さないので、余分なファイルの検出はこのテストの責務
    expect(committedNames).toEqual([...builtPaths]);
  });
});

describe('照合データの中身（コミット済みの JSON と作り直した内容）', () => {
  for (const file of builtFiles) {
    it(`${file.path} の中身が一致する`, () => {
      expect(committedNames).toContain(file.path);
      // 整形ではなく構造で比べる（CRLF に変換されていても落ちない）
      expect(readFixtureJson(file.path)).toEqual(file.value);
    });
  }
});

describe('照合データの整形', () => {
  for (const file of builtFiles) {
    it(`${file.path} の整形が stringifyFixture と同じ（index.json だけ indent 2、他は 1 行）`, () => {
      const text = readFixtureText(file.path);
      const expected = stringifyFixture(file);
      expect(text.endsWith('\n')).toBe(true);
      expect(text.split('\n')).toHaveLength(expected.split('\n').length);
    });
  }
});

describe('index.json の整合', () => {
  it('定数と一致する', () => {
    const index = readFixtureJson(INDEX_FILE) as JsonIndex;
    expect(index.formatVersion).toBe(FORMAT_VERSION);
    expect(index.orientationCount).toBe(ORIENTATION_COUNT);
    expect(index.puzzleSeeds).toEqual([...PUZZLE_SEEDS]);
    expect(index.rngSeeds).toEqual([...RNG_SEEDS]);
    expect(index.rngStreamLength).toBe(RNG_STREAM_LENGTH);
    expect(index.hintCount).toBe(HINT_COUNT);
  });

  it('files が index.json 以外の全ファイル名の昇順', () => {
    const index = readFixtureJson(INDEX_FILE) as JsonIndex;
    const expected = builtPaths.filter((path): boolean => path !== INDEX_FILE);
    expect(index.files).toEqual(expected);
    expect(index.files).toEqual([...index.files].sort(comparePath));
  });

  it(`combinations が ${EXPECTED_COMBINATION_COUNT} 件で N=3..7 × piecePresets(n) を覆う`, () => {
    const index = readFixtureJson(INDEX_FILE) as JsonIndex;
    expect(index.combinations).toHaveLength(EXPECTED_COMBINATION_COUNT);
    expect(index.combinations).toEqual(expectedCombinations());
  });
});

describe('各パズルファイルのケース', () => {
  for (const combination of expectedCombinations()) {
    const { n, m, file } = combination;

    it(`${file} のケースが 回転 2 通り × PUZZLE_SEEDS の 6 件`, () => {
      const puzzle = readFixtureJson(file) as JsonPuzzleFile;
      expect(puzzle.n).toBe(n);
      expect(puzzle.m).toBe(m);

      type CaseKey = { seed: number; allowRotation: boolean };
      const expectedCases = [false, true].flatMap((allowRotation): CaseKey[] =>
        PUZZLE_SEEDS.map((seed): CaseKey => ({ seed, allowRotation })),
      );
      const actualCases = puzzle.cases.map(
        (entry): CaseKey => ({ seed: entry.seed, allowRotation: entry.allowRotation }),
      );
      expect(actualCases).toEqual(expectedCases);
    });

    it(`${file} の「パズルの回転なし」のケースは全ピースの向きが恒等`, () => {
      const puzzle = readFixtureJson(file) as JsonPuzzleFile;
      for (const entry of puzzle.cases) {
        if (entry.allowRotation) continue;
        for (const key of PLACEMENT_KEYS) {
          for (const placement of entry[key]) {
            // RULES.md 3.1「パズルの回転」なし: 回転していない向きのまま散らす
            expect(
              placement.orientation,
              `${file} seed=${entry.seed} ${key} piece=${placement.pieceId}`,
            ).toBe(IDENTITY_ORIENTATION);
          }
        }
      }
    });

    it(`${file} の各ケースの中身が揃っている`, () => {
      const puzzle = readFixtureJson(file) as JsonPuzzleFile;
      const expectedIds = rangeIds(m);

      for (const entry of puzzle.cases) {
        const label = `${file} seed=${entry.seed} allowRotation=${entry.allowRotation}`;

        // ピースは M 個、id は 0..M-1。voxels は局所座標で、局所原点 (0,0,0) を必ず含む
        expect(
          entry.pieces.map((piece): number => piece.id),
          label,
        ).toEqual(expectedIds);
        for (const piece of entry.pieces) {
          expect(piece.voxels.length, `${label} piece=${piece.id}`).toBeGreaterThan(0);
          const hasOrigin = piece.voxels.some(
            (voxel): boolean => voxel[0] === 0 && voxel[1] === 0 && voxel[2] === 0,
          );
          expect(hasOrigin, `${label} piece=${piece.id} の局所原点`).toBe(true);
        }

        // 配置の配列はどれも M 件で、ピース id が 0..M-1 を 1 回ずつ
        for (const key of PLACEMENT_KEYS) {
          const placements = entry[key];
          expect(placements, `${label} ${key}`).toHaveLength(m);
          expect(
            placements.map((placement): number => placement.pieceId).sort(compareNumber),
            `${label} ${key}`,
          ).toEqual(expectedIds);
          for (const placement of placements) {
            const at = `${label} ${key} piece=${placement.pieceId}`;
            expect(placement.position, at).toHaveLength(3);
            expect(placement.orientation, at).toBeGreaterThanOrEqual(0);
            expect(placement.orientation, at).toBeLessThan(ORIENTATION_COUNT);
          }
        }

        // ヒントは HINT_COUNT 回まで、同じピースを 2 度選ばない
        expect(entry.hintPieceIds.length, `${label} hintPieceIds`).toBeLessThanOrEqual(HINT_COUNT);
        expect(new Set(entry.hintPieceIds).size, `${label} hintPieceIds の重複`).toBe(
          entry.hintPieceIds.length,
        );

        // ヒントで固定したピースは散らし直しても解答の配置に残る（RULES.md 3.3「やり直し」/ 3.7）
        for (const pieceId of entry.hintPieceIds) {
          expect(placementOf(entry.reshuffleWithHints, pieceId), `${label} hint piece=${pieceId}`).toEqual(
            placementOf(entry.solution, pieceId),
          );
        }

        // ヒントの固定が無い散らし直しは最初の散らしと同じ配置（RULES.md 3.3「やり直し」）。
        // reshuffleWithoutHints は keep 無しで作るので、hintPieceIds が何であれ一致する
        expect(entry.reshuffleWithoutHints, `${label} reshuffleWithoutHints`).toEqual(entry.scatter);
      }
    });
  }
});
