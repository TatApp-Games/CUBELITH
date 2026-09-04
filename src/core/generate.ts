// 領域拡張（Region Growing）による分割と、プレイ開始時の初期散らし（SPEC.md 3.2）。
// Three.js に依存しない純粋なロジック。乱数は必ず createRng を通す（Math.random を使わない）。

import {
  addVec3,
  compareVec3,
  IDENTITY_ORIENTATION,
  ORIENTATION_COUNT,
  vec3,
  vec3Key,
  type Vec3,
} from './grid';
import { createPiece, localOrigin, placedVoxels, type Piece, type Placement } from './piece';
import { createRng, type Rng } from './rng';
import { isSolved } from './solve';

/** 空間サイズ N の下限（SPEC.md 3.1）。 */
export const MIN_SPACE_SIZE = 3;
/** 空間サイズ N の上限（SPEC.md 3.1）。 */
export const MAX_SPACE_SIZE = 7;
/** 分割数 M の下限（SPEC.md 3.1）。 */
export const MIN_PIECE_COUNT = 2;

// 分割数の絶対上限。maxPieces() のコメントを参照。
const PIECE_COUNT_CAP = 40;

// 未割り当てのボクセルを表す owner の値。
const UNASSIGNED = -1;

// 散らしで 1 ピースを置くときのリトライ上限と、範囲を広げる間隔。
const SCATTER_ATTEMPT_LIMIT = 10000;
const SCATTER_WIDEN_INTERVAL = 100;
// 散らし全体（クリア状態を避けるための引き直し）の上限。
const SCATTER_RETRY_LIMIT = 8;

/** 6 近傍のオフセット。 */
const NEIGHBOR_OFFSETS: readonly Vec3[] = [
  vec3(1, 0, 0),
  vec3(-1, 0, 0),
  vec3(0, 1, 0),
  vec3(0, -1, 0),
  vec3(0, 0, 1),
  vec3(0, 0, -1),
];

/**
 * 生成結果。
 * pieces は局所座標へ正規化済み（局所原点が (0,0,0)）で、solution は各ピースの解答配置。
 * pieces[i] と solution[i] は同じピース（id は i）を指す。
 */
export type GeneratedPuzzle = {
  readonly n: number;
  readonly m: number;
  readonly seed: number;
  readonly pieces: readonly Piece[];
  readonly solution: readonly Placement[];
};

/** 配列の要素を取り出す。範囲外は実装のバグなので例外にする（noUncheckedIndexedAccess 対策）。 */
function at<T>(array: readonly T[], index: number): T {
  const value = array[index];
  if (value === undefined) throw new Error(`配列の範囲外アクセス: ${index}`);
  return value;
}

/** N が 3..7 の整数であることを確かめる。 */
function assertSpaceSize(n: number): void {
  if (!Number.isInteger(n) || n < MIN_SPACE_SIZE || n > MAX_SPACE_SIZE) {
    throw new RangeError(
      `空間サイズ N は ${MIN_SPACE_SIZE}..${MAX_SPACE_SIZE} の整数 (got ${String(n)})`,
    );
  }
}

/** シードが整数であることを確かめる。 */
function assertSeed(seed: number): void {
  if (!Number.isInteger(seed)) throw new RangeError(`シードは整数 (got ${String(seed)})`);
}

/**
 * 空間サイズ N に対して選べる分割数 M の上限。
 *
 * 解釈: SPEC.md 3.1 は「2 〜 N³ / 4 程度」としつつ、N=3 で 2〜6、N=7 で 2〜40 を目安に挙げている。
 * 後者は N³/4 = 85 と合わないので、両方を満たす min(floor(N³/4), 40) を上限として採用する
 * （N=3 → 6、N=4 → 16、N=5 → 31、N=6 → 40、N=7 → 40）。
 */
export function maxPieces(n: number): number {
  assertSpaceSize(n);
  return Math.min(Math.floor(n ** 3 / 4), PIECE_COUNT_CAP);
}

/** M が 2..maxPieces(n) の整数であることを確かめる。 */
function assertPieceCount(n: number, m: number): void {
  const limit = maxPieces(n);
  if (!Number.isInteger(m) || m < MIN_PIECE_COUNT || m > limit) {
    throw new RangeError(`分割数 M は ${MIN_PIECE_COUNT}..${limit} の整数 (N=${n}, got ${String(m)})`);
  }
}

/** グリッド座標 → 一次元インデックス。 */
function encodeIndex(v: Vec3, n: number): number {
  return (v.x * n + v.y) * n + v.z;
}

/** 一次元インデックス → グリッド座標。 */
function decodeIndex(index: number, n: number): Vec3 {
  return vec3(Math.floor(index / (n * n)), Math.floor(index / n) % n, index % n);
}

/** 座標が N×N×N の中にあるか。 */
function isInside(v: Vec3, n: number): boolean {
  return v.x >= 0 && v.x < n && v.y >= 0 && v.y < n && v.z >= 0 && v.z < n;
}

/**
 * 領域拡張で N×N×N を M 個の連結なピースに分割する（SPEC.md 3.2）。
 * 返す pieces は局所座標に正規化済みで、絶対座標（解答位置）は solution が持つ。
 */
export function generatePuzzle(n: number, m: number, seed: number): GeneratedPuzzle {
  assertSpaceSize(n);
  assertPieceCount(n, m);
  assertSeed(seed);

  const rng = createRng(seed);
  const total = n * n * n;
  // owner[index] = そのボクセルを持つピース id（未割り当ては UNASSIGNED）
  const owner: number[] = new Array<number>(total).fill(UNASSIGNED);
  // members[piece] = そのピースのボクセルのインデックス
  const members: number[][] = Array.from({ length: m }, (): number[] => []);
  // frontiers[piece] = 吸収候補（隣接する未割り当てボクセル）。割り当て済みの古い候補も混ざる
  const frontiers: number[][] = Array.from({ length: m }, (): number[] => []);

  /** ボクセルをピースに吸収し、その 6 近傍の空きを候補に積む。 */
  const absorb = (index: number, piece: number): void => {
    owner[index] = piece;
    at(members, piece).push(index);
    const v = decodeIndex(index, n);
    const frontier = at(frontiers, piece);
    for (const offset of NEIGHBOR_OFFSETS) {
      const neighbor = addVec3(v, offset);
      if (!isInside(neighbor, n)) continue;
      const neighborIndex = encodeIndex(neighbor, n);
      if (at(owner, neighborIndex) !== UNASSIGNED) continue;
      frontier.push(neighborIndex);
    }
  };

  // 2. 相異なるランダム座標に M 個のシードを置く。
  //    部分 Fisher-Yates で先頭 M 個を選ぶので、重複のリトライ無しに必ず相異なる。
  const shuffled: number[] = Array.from({ length: total }, (_, i): number => i);
  for (let i = 0; i < m; i++) {
    const j = i + rng.nextInt(total - i);
    const a = at(shuffled, i);
    const b = at(shuffled, j);
    shuffled[i] = b;
    shuffled[j] = a;
    absorb(b, i);
  }

  // 3. 各ピースが隣接する空きを 1 つ吸収する、をラウンドロビンで繰り返す。
  let assigned = m;
  while (assigned < total) {
    let grew = false;
    for (let piece = 0; piece < m && assigned < total; piece++) {
      const frontier = at(frontiers, piece);
      let picked = UNASSIGNED;
      while (frontier.length > 0) {
        const k = rng.nextInt(frontier.length);
        const candidate = at(frontier, k);
        frontier[k] = at(frontier, frontier.length - 1);
        frontier.pop();
        if (at(owner, candidate) === UNASSIGNED) {
          picked = candidate;
          break;
        }
      }
      // 空きが無いピースはこのラウンドをスキップ
      if (picked === UNASSIGNED) continue;
      absorb(picked, piece);
      assigned++;
      grew = true;
    }
    if (grew) continue;

    // すべてのピースがスキップになったのに空きが残る場合（SPEC.md 3.2-3 の孤立した空き）。
    // 候補は吸収されるまで frontier に残る作りなので実際にはここへ来ないが、仕様どおり保険を置く。
    let remaining: number[] = [];
    for (let index = 0; index < total; index++) {
      if (at(owner, index) === UNASSIGNED) remaining.push(index);
    }
    while (remaining.length > 0) {
      const next: number[] = [];
      let progressed = false;
      for (const index of remaining) {
        const v = decodeIndex(index, n);
        let host = UNASSIGNED;
        for (const offset of NEIGHBOR_OFFSETS) {
          const neighbor = addVec3(v, offset);
          if (!isInside(neighbor, n)) continue;
          const neighborOwner = at(owner, encodeIndex(neighbor, n));
          if (neighborOwner !== UNASSIGNED) {
            host = neighborOwner;
            break;
          }
        }
        // どのピースにも隣接しない空きは次の周回に回す（周りが埋まれば吸収できる）
        if (host === UNASSIGNED) {
          next.push(index);
          continue;
        }
        absorb(index, host);
        assigned++;
        progressed = true;
      }
      if (!progressed) throw new Error('生成に失敗: どのピースにも隣接しない空きが残った');
      remaining = next;
    }
  }

  // 4. 解答位置（絶対座標）から局所座標へ正規化し、正規化で引いたオフセットを解答配置にする。
  const pieces: Piece[] = [];
  const solution: Placement[] = [];
  for (let id = 0; id < m; id++) {
    const absolute = at(members, id)
      .map((index): Vec3 => decodeIndex(index, n))
      .sort(compareVec3);
    pieces.push(createPiece(id, absolute));
    solution.push({
      pieceId: id,
      orientation: IDENTITY_ORIENTATION,
      position: localOrigin(absolute),
    });
  }

  return { n, m, seed, pieces, solution };
}

/** 領域拡張で分割したピース（局所座標に正規化済み）。解答位置は solutionPlacements で得る。 */
export function generatePieces(n: number, m: number, seed: number): Piece[] {
  return generatePuzzle(n, m, seed).pieces.slice();
}

/**
 * 各ピースの解答配置（向きは恒等、位置は生成時の絶対座標のオフセット）。
 *
 * 解釈: 正規化でオフセットが落ちるため Piece だけからは解答位置を復元できない。
 * そこで generatePuzzle が pieces と solution を対で返す形にし、この関数は同じ引数で
 * 解答配置だけを取り出す薄い入口とする（両方要るときは generatePuzzle を 1 回呼べばよい）。
 */
export function solutionPlacements(n: number, m: number, seed: number): Placement[] {
  return generatePuzzle(n, m, seed).solution.slice();
}

/** 1 回分の散らし。重なりが出たらその場でリトライし、詰まったら範囲を広げる。 */
function scatterOnce(pieces: readonly Piece[], n: number, rng: Rng): Placement[] {
  const occupied = new Set<string>();
  const placements: Placement[] = [];
  // 立方体は [0, N-1]³ にあるので、その中心のまわり ±(N+2) を既定の散らし範囲にする
  const center = Math.floor((n - 1) / 2);

  for (const piece of pieces) {
    let half = n + 2;
    let attempts = 0;
    for (;;) {
      const span = half * 2 + 1;
      const placement: Placement = {
        pieceId: piece.id,
        orientation: rng.nextInt(ORIENTATION_COUNT),
        position: vec3(
          center - half + rng.nextInt(span),
          center - half + rng.nextInt(span),
          center - half + rng.nextInt(span),
        ),
      };
      const keys = placedVoxels(piece, placement).map(vec3Key);
      if (keys.every((key): boolean => !occupied.has(key))) {
        for (const key of keys) occupied.add(key);
        placements.push(placement);
        break;
      }
      attempts++;
      if (attempts >= SCATTER_ATTEMPT_LIMIT) {
        throw new Error(`散らしに失敗: ピース ${piece.id} を置けなかった`);
      }
      // 詰まってきたら範囲を広げ、必ず終わるようにする
      if (attempts % SCATTER_WIDEN_INTERVAL === 0) half++;
    }
  }
  return placements;
}

/**
 * プレイ開始時の初期散らし（SPEC.md 3.2-5）。
 * 各ピースにランダムな向き（24 通り）と、立方体の周囲 ±(N+2) 程度のランダムな位置を与える。
 * ピース同士は重ならない。同じ引数なら常に同じ配置になる。
 *
 * 解釈: 散らした直後にクリア判定が真になると開始と同時にクリアしてしまうので、真なら引き直す。
 */
export function scatterPlacements(pieces: readonly Piece[], n: number, seed: number): Placement[] {
  assertSpaceSize(n);
  assertSeed(seed);
  if (pieces.length === 0) throw new RangeError('散らし: ピースが空');

  const rng = createRng(seed);
  for (let retry = 0; retry < SCATTER_RETRY_LIMIT; retry++) {
    const placements = scatterOnce(pieces, n, rng);
    if (!isSolved(pieces, placements, n)) return placements;
  }
  throw new Error('散らしに失敗: クリア状態でない配置を作れなかった');
}
