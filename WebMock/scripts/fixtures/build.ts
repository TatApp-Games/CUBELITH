// UE 版と照合するデータ（golden fixtures）の組み立て（SPEC_UE.md 7.3）。
// 「同じ条件とシードなら Web 版と UE 版で同じパズルを出す」（RULES.md 3.6）ことを、
// UE 版のテストがこのデータとの一致で確かめる。
//
// このモジュールは fs に触らない純粋な組み立てだけを担い、書き出しは scripts/exportFixtures.ts が行う。
// 同じ入力からは常に同じ内容を返す（日時・実行環境・パッケージのバージョンなどは一切入れない）。

import {
  generatePuzzle,
  MAX_SPACE_SIZE,
  MIN_SPACE_SIZE,
  scatterPlacements,
} from '../../src/core/generate';
import { replacePlacement } from '../../src/core/game';
import {
  composeOrientation,
  ORIENTATION_COUNT,
  orientationMatrix,
  type Vec3,
} from '../../src/core/grid';
import { pickHintPiece } from '../../src/core/hint';
import type { Piece, Placement } from '../../src/core/piece';
import { createRng } from '../../src/core/rng';
import { piecePresets } from '../../src/ui/difficulty';

// ---- 定数（文書とテストからも参照する） ----

/** 照合データの形式の版。JSON の形（キー・並び）を変えたら上げる。 */
export const FORMAT_VERSION = 1;

/**
 * パズルを書き出すシード。
 *
 * 解釈: 要望ではシードの個数と値は未定なので、小さい値・日付由来の値・uint32 の最大値の 3 つにして、
 * `seed >>> 0` まわりの取りこぼしを拾えるようにする。
 */
export const PUZZLE_SEEDS: readonly number[] = [1, 20260926, 4294967295];

/** 乱数の出力列を書き出すシード。出力列だけは seed 0 も入れる。 */
export const RNG_SEEDS: readonly number[] = [0, 1, 20260926, 4294967295];

/** 乱数の出力列の長さ。 */
export const RNG_STREAM_LENGTH = 64;

/** 1 ケースで固定するヒントの回数（RULES.md 3.7）。 */
export const HINT_COUNT = 2;

/** 目録のファイル名。 */
export const INDEX_FILE = 'index.json';

/** index.json の generator に入れる書き出しの入口。 */
const GENERATOR = 'WebMock/scripts/exportFixtures.ts';

/** mulberry32 の生の出力を整数に戻す係数（next() は [0,1) にした値を返す）。 */
const UINT32_SCALE = 4294967296;

// ---- 出力の型 ----

/** 1 ファイル分の出力。path は Fixtures/ からの相対パス。pretty=true なら indent 2 で書く。 */
export type FixtureFile = { readonly path: string; readonly value: unknown; readonly pretty: boolean };

/** 座標は [x, y, z] の整数 3 要素配列。 */
type JsonVec3 = readonly [number, number, number];

/** 配置。キーの順は pieceId → orientation → position で固定する。 */
type JsonPlacement = {
  readonly pieceId: number;
  readonly orientation: number;
  readonly position: JsonVec3;
};

/** ピース。voxels は局所座標（局所原点が (0,0,0)）で、並びは core が返す順のまま。 */
type JsonPiece = { readonly id: number; readonly voxels: readonly JsonVec3[] };

/** 1 ケース（回転の可否 × シード）。 */
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

/** N・M の組み合わせと、その出力ファイル名。 */
type JsonCombination = { readonly n: number; readonly m: number; readonly file: string };

/** 1 シード分の乱数の出力列。 */
type JsonRngStream = {
  readonly seed: number;
  readonly uint32: readonly number[];
  readonly nextInt24: readonly number[];
  readonly nextInt1000: readonly number[];
};

// ---- 変換の小物 ----

/** 文字列の昇順比較。ロケールに依らず決定的にするため素の比較を使う。 */
function comparePath(a: string, b: string): number {
  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

function toJsonVec3(v: Vec3): JsonVec3 {
  return [v.x, v.y, v.z];
}

function toJsonPlacement(placement: Placement): JsonPlacement {
  return {
    pieceId: placement.pieceId,
    orientation: placement.orientation,
    position: toJsonVec3(placement.position),
  };
}

function toJsonPiece(piece: Piece): JsonPiece {
  return { id: piece.id, voxels: piece.voxels.map(toJsonVec3) };
}

function toJsonPlacements(placements: readonly Placement[]): JsonPlacement[] {
  return placements.map(toJsonPlacement);
}

/** N・M からパズルのファイル名。 */
function puzzleFileName(n: number, m: number): string {
  return `puzzle_n${n}_m${m}.json`;
}

// ---- 各ファイルの中身 ----

/** 向きの表（RULES.md 3.3）。matrices[i] は orientationMatrix(i)（行優先）。 */
function buildOrientations(): {
  readonly count: number;
  readonly matrices: readonly (readonly number[])[];
  readonly compose: readonly number[];
} {
  const matrices: number[][] = [];
  for (let id = 0; id < ORIENTATION_COUNT; id += 1) matrices.push([...orientationMatrix(id)]);

  // compose[a * 24 + b] = 「a を適用してから b」（src/core/grid.ts の composeOrientation と同じ順）
  const compose: number[] = [];
  for (let a = 0; a < ORIENTATION_COUNT; a += 1) {
    for (let b = 0; b < ORIENTATION_COUNT; b += 1) compose.push(composeOrientation(a, b));
  }

  return { count: ORIENTATION_COUNT, matrices, compose };
}

/**
 * 1 シード分の乱数の出力列（RULES.md 3.6）。
 *
 * 解釈: next() が返す [0,1) の小数を JSON に書くと桁の丸めで比べにくいので、mulberry32 の生の
 * uint32 を整数で記録する（UE 側は uint32 のまま比べられる）。
 * 3 つの列はそれぞれ新しい createRng(seed) から取る（互いに乱数を食い合わせない）。
 */
function buildRngStream(seed: number): JsonRngStream {
  const uint32: number[] = [];
  const rawRng = createRng(seed);
  for (let i = 0; i < RNG_STREAM_LENGTH; i += 1) uint32.push(rawRng.next() * UINT32_SCALE);

  const nextInt24: number[] = [];
  const int24Rng = createRng(seed);
  for (let i = 0; i < RNG_STREAM_LENGTH; i += 1) nextInt24.push(int24Rng.nextInt(ORIENTATION_COUNT));

  const nextInt1000: number[] = [];
  const int1000Rng = createRng(seed);
  for (let i = 0; i < RNG_STREAM_LENGTH; i += 1) nextInt1000.push(int1000Rng.nextInt(1000));

  return { seed, uint32, nextInt24, nextInt1000 };
}

function buildRng(): {
  readonly streamLength: number;
  readonly streams: readonly JsonRngStream[];
} {
  return { streamLength: RNG_STREAM_LENGTH, streams: RNG_SEEDS.map(buildRngStream) };
}

/**
 * 1 ケース分。生成 → 初期散らし → 散らし直し（ヒント無し / あり）をこの順で作る。
 * この手順は後続タスクの Docs/FIXTURES.md にも載るので、変えるときは文書も直す。
 */
function buildCase(n: number, m: number, seed: number, allowRotation: boolean): JsonCase {
  const { pieces, solution } = generatePuzzle(n, m, seed);

  const solutionById = new Map<number, Placement>();
  for (const placement of solution) solutionById.set(placement.pieceId, placement);
  const solutionOf = (pieceId: number): Placement => {
    const placement = solutionById.get(pieceId);
    if (placement === undefined) throw new Error(`解答に無いピース id ${pieceId}`);
    return placement;
  };

  const scatter = scatterPlacements(pieces, n, seed, { allowRotation });
  const reshuffleWithoutHints = scatterPlacements(pieces, n, seed, { allowRotation, keep: [] });

  // ヒントの固定があるときの散らし直し（RULES.md 3.3「やり直し」/ 3.7）。
  // HINT_COUNT 回分、pickHintPiece で対象を選び、解答の配置へ置いて固定した状態を作る。
  // pickHintPiece が null を返したらそこで打ち切る（未固定が 1 個以下では使えない）。
  const hintPieceIds: number[] = [];
  let current: readonly Placement[] = scatter;
  for (let i = 0; i < HINT_COUNT; i += 1) {
    const id = pickHintPiece(current, solution, hintPieceIds);
    if (id === null) break;
    current = replacePlacement(current, solutionOf(id));
    hintPieceIds.push(id);
  }
  // keep は hintPieceIds の順で渡す（scatterPlacements は keep の並びでは乱数を消費しないが、
  // 順序も記録どおりに固定しておく）。
  const keep = hintPieceIds.map(solutionOf);
  const reshuffleWithHints = scatterPlacements(pieces, n, seed, { allowRotation, keep });

  return {
    seed,
    allowRotation,
    pieces: pieces.map(toJsonPiece),
    solution: toJsonPlacements(solution),
    scatter: toJsonPlacements(scatter),
    reshuffleWithoutHints: toJsonPlacements(reshuffleWithoutHints),
    hintPieceIds: [...hintPieceIds],
    reshuffleWithHints: toJsonPlacements(reshuffleWithHints),
  };
}

/** 1 つの N・M のファイル。ケースの並びは allowRotation が false → true、その中で PUZZLE_SEEDS の順。 */
function buildPuzzleFile(n: number, m: number): {
  readonly n: number;
  readonly m: number;
  readonly cases: readonly JsonCase[];
} {
  const cases: JsonCase[] = [];
  for (const allowRotation of [false, true]) {
    for (const seed of PUZZLE_SEEDS) cases.push(buildCase(n, m, seed, allowRotation));
  }
  return { n, m, cases };
}

/** 書き出す N・M の 25 通り。表を書き写さず piecePresets(n) から作る（RULES.md 3.1）。 */
function buildCombinations(): JsonCombination[] {
  const combinations: JsonCombination[] = [];
  for (let n = MIN_SPACE_SIZE; n <= MAX_SPACE_SIZE; n += 1) {
    const presets = [...piecePresets(n)].sort((a, b): number => a - b);
    for (const m of presets) combinations.push({ n, m, file: puzzleFileName(n, m) });
  }
  return combinations;
}

// ---- 公開 API ----

/** 書き出す全ファイル。path の昇順で返す（並びも決定的にする）。 */
export function buildFixtureFiles(): FixtureFile[] {
  const files: FixtureFile[] = [
    { path: 'orientations.json', value: buildOrientations(), pretty: false },
    { path: 'rng.json', value: buildRng(), pretty: false },
  ];

  const combinations = buildCombinations();
  for (const combination of combinations) {
    files.push({
      path: combination.file,
      value: buildPuzzleFile(combination.n, combination.m),
      pretty: false,
    });
  }

  // 目録。files は index.json 以外の全ファイル名を昇順で、combinations は N・M の昇順で入れる
  const index = {
    formatVersion: FORMAT_VERSION,
    generator: GENERATOR,
    orientationCount: ORIENTATION_COUNT,
    puzzleSeeds: [...PUZZLE_SEEDS],
    rngSeeds: [...RNG_SEEDS],
    rngStreamLength: RNG_STREAM_LENGTH,
    hintCount: HINT_COUNT,
    files: files.map((file): string => file.path).sort(comparePath),
    combinations,
  };
  files.push({ path: INDEX_FILE, value: index, pretty: true });

  return files.sort((a, b): number => comparePath(a.path, b.path));
}

/** FixtureFile を文字列にする。pretty のとき indent 2、それ以外は空白なし。末尾に改行 1 つ、改行は LF。 */
export function stringifyFixture(file: FixtureFile): string {
  const text = file.pretty ? JSON.stringify(file.value, null, 2) : JSON.stringify(file.value);
  return `${text}\n`;
}
