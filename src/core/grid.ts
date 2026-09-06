// ボクセル座標と 90 度単位の向き（24 通り）。Three.js に依存しない純粋なロジック（SPEC.md 3.3）。
// 座標は整数 (x, y, z) で Y が上。向きは行列式 +1 の整数回転行列 24 個に id 0..23 を振って表す。

/** 整数のボクセル座標。Y が上。 */
export type Vec3 = { readonly x: number; readonly y: number; readonly z: number };

/** 行優先の 3x3 整数行列。列ベクトルに左から掛ける（v' = M v）。 */
export type Mat3 = readonly [
  number, number, number,
  number, number, number,
  number, number, number,
];

/** 回転軸。UI の Pitch = x / Yaw = y / Roll = z に対応する。 */
export type Axis = 'x' | 'y' | 'z';

/** 90 度単位の向きの総数。 */
export const ORIENTATION_COUNT = 24;

/** 恒等（無回転）の向き id。 */
export const IDENTITY_ORIENTATION = 0;

/** Vec3 を作る。 */
export function vec3(x: number, y: number, z: number): Vec3 {
  return { x, y, z };
}

/** 和 a + b。 */
export function addVec3(a: Vec3, b: Vec3): Vec3 {
  return { x: a.x + b.x, y: a.y + b.y, z: a.z + b.z };
}

/** 差 a - b。 */
export function subVec3(a: Vec3, b: Vec3): Vec3 {
  return { x: a.x - b.x, y: a.y - b.y, z: a.z - b.z };
}

/** 座標が等しいか。 */
export function equalsVec3(a: Vec3, b: Vec3): boolean {
  return a.x === b.x && a.y === b.y && a.z === b.z;
}

/** 辞書順（x → y → z）の比較。負 / 0 / 正を返す。並びを決定的にするために使う。 */
export function compareVec3(a: Vec3, b: Vec3): number {
  if (a.x !== b.x) return a.x - b.x;
  if (a.y !== b.y) return a.y - b.y;
  return a.z - b.z;
}

/** Set / Map のキーにする文字列。 */
export function vec3Key(v: Vec3): string {
  return `${v.x},${v.y},${v.z}`;
}

// ---- 以下はモジュール内部の実装（向きの表の構築） ----

const IDENTITY_MATRIX: Mat3 = [1, 0, 0, 0, 1, 0, 0, 0, 1];

// 各軸まわりの +90 度回転（右手系）
const ROTATION_X: Mat3 = [1, 0, 0, 0, 0, -1, 0, 1, 0];
const ROTATION_Y: Mat3 = [0, 0, 1, 0, 1, 0, -1, 0, 0];
const ROTATION_Z: Mat3 = [0, -1, 0, 1, 0, 0, 0, 0, 1];

/** 行列積 a·b。 */
function multiplyMat3(a: Mat3, b: Mat3): Mat3 {
  const [a0, a1, a2, a3, a4, a5, a6, a7, a8] = a;
  const [b0, b1, b2, b3, b4, b5, b6, b7, b8] = b;
  return [
    a0 * b0 + a1 * b3 + a2 * b6, a0 * b1 + a1 * b4 + a2 * b7, a0 * b2 + a1 * b5 + a2 * b8,
    a3 * b0 + a4 * b3 + a5 * b6, a3 * b1 + a4 * b4 + a5 * b7, a3 * b2 + a4 * b5 + a5 * b8,
    a6 * b0 + a7 * b3 + a8 * b6, a6 * b1 + a7 * b4 + a8 * b7, a6 * b2 + a7 * b5 + a8 * b8,
  ];
}

/** 転置。回転行列では逆回転にあたる。 */
function transposeMat3(m: Mat3): Mat3 {
  const [m0, m1, m2, m3, m4, m5, m6, m7, m8] = m;
  return [m0, m3, m6, m1, m4, m7, m2, m5, m8];
}

function mat3Key(m: Mat3): string {
  return m.join(',');
}

/**
 * 恒等から 3 軸の 90 度回転で閉包を取り、24 個の回転行列を一度だけ構築する。
 * 幅優先で辿るので id 0 は必ず恒等になり、列挙順は決定的。生成元が回転のみなので鏡像は入らない。
 */
function buildOrientationMatrices(): readonly Mat3[] {
  const generators: readonly Mat3[] = [ROTATION_X, ROTATION_Y, ROTATION_Z];
  const matrices: Mat3[] = [IDENTITY_MATRIX];
  const seen = new Set<string>([mat3Key(IDENTITY_MATRIX)]);
  for (let head = 0; head < matrices.length; head++) {
    const current = matrices[head];
    if (current === undefined) continue;
    for (const generator of generators) {
      const next = multiplyMat3(generator, current);
      const key = mat3Key(next);
      if (seen.has(key)) continue;
      seen.add(key);
      matrices.push(next);
    }
  }
  if (matrices.length !== ORIENTATION_COUNT) {
    throw new Error(`向きの列挙に失敗: ${matrices.length} 個になった`);
  }
  return matrices;
}

const ORIENTATION_MATRICES: readonly Mat3[] = buildOrientationMatrices();

const ORIENTATION_BY_MATRIX: ReadonlyMap<string, number> = new Map(
  ORIENTATION_MATRICES.map((m, index): [string, number] => [mat3Key(m), index]),
);

/** 回転行列 → 向き id。24 通りの外の行列は例外。 */
function orientationIdOf(m: Mat3): number {
  const id = ORIENTATION_BY_MATRIX.get(mat3Key(m));
  if (id === undefined) throw new RangeError(`90 度単位の回転ではない行列: ${mat3Key(m)}`);
  return id;
}

/** 向き id が 0..23 の整数であることを確かめてそのまま返す。 */
function assertOrientation(orientation: number): number {
  if (!Number.isInteger(orientation) || orientation < 0 || orientation >= ORIENTATION_COUNT) {
    throw new RangeError(`向き id は 0..${ORIENTATION_COUNT - 1} の整数 (got ${orientation})`);
  }
  return orientation;
}

/** 合成の表。COMPOSE_TABLE[a * 24 + b] = 「a を適用してから b を適用した向き」。 */
function buildComposeTable(): readonly number[] {
  const table: number[] = [];
  for (let a = 0; a < ORIENTATION_COUNT; a++) {
    for (let b = 0; b < ORIENTATION_COUNT; b++) {
      const ma = ORIENTATION_MATRICES[a];
      const mb = ORIENTATION_MATRICES[b];
      if (ma === undefined || mb === undefined) throw new Error('向きの表が壊れている');
      table.push(orientationIdOf(multiplyMat3(mb, ma)));
    }
  }
  return table;
}

const COMPOSE_TABLE: readonly number[] = buildComposeTable();

/** 軸ごとの ±90 度回転に対応する向き id。 */
const AXIS_ROTATION_ID: Readonly<Record<Axis, { readonly plus: number; readonly minus: number }>> = {
  x: { plus: orientationIdOf(ROTATION_X), minus: orientationIdOf(transposeMat3(ROTATION_X)) },
  y: { plus: orientationIdOf(ROTATION_Y), minus: orientationIdOf(transposeMat3(ROTATION_Y)) },
  z: { plus: orientationIdOf(ROTATION_Z), minus: orientationIdOf(transposeMat3(ROTATION_Z)) },
};

// ---- 向きの公開 API ----

/** 向き id → 3x3 の整数回転行列（行列式 +1）。 */
export function orientationMatrix(orientation: number): Mat3 {
  const m = ORIENTATION_MATRICES[assertOrientation(orientation)];
  if (m === undefined) throw new RangeError(`向きの表に id ${orientation} が無い`);
  return m;
}

/** 座標に向きを適用する。整数座標は整数座標のまま。 */
export function rotateVoxel(v: Vec3, orientation: number): Vec3 {
  const [m0, m1, m2, m3, m4, m5, m6, m7, m8] = orientationMatrix(orientation);
  return {
    x: m0 * v.x + m1 * v.y + m2 * v.z,
    y: m3 * v.x + m4 * v.y + m5 * v.z,
    z: m6 * v.x + m7 * v.y + m8 * v.z,
  };
}

/** 向き a を適用してから b を適用した向き（行列では Mb·Ma）。表引きで返す。 */
export function composeOrientation(a: number, b: number): number {
  const id = COMPOSE_TABLE[assertOrientation(a) * ORIENTATION_COUNT + assertOrientation(b)];
  if (id === undefined) throw new RangeError(`合成の表に (${a}, ${b}) が無い`);
  return id;
}

/**
 * 現在の向きに、ワールドの軸まわりの 90 度回転を 1 段重ねた向きを返す。
 * SPEC.md 3.3 の Pitch / Yaw / Roll の ± ボタンはこれを呼ぶ。
 */
export function rotateOrientation(orientation: number, axis: Axis, dir: 1 | -1): number {
  const step = AXIS_ROTATION_ID[axis];
  return composeOrientation(orientation, dir === 1 ? step.plus : step.minus);
}

/**
 * 任意の 3x3 実数行列に最も近い 90 度単位の向き id を返す。
 *
 * 24 個の向き行列との Frobenius 内積（対応する要素どうしの積の和）が最大のものを選ぶ。
 * 回転行列どうしの内積は 1 + 2cosθ（θ は回転角の差）なので、これは「回転角の差が最小」と同じ。
 * 同点なら id の小さい方を返し、結果を決定的にする。
 *
 * 用途: 回転モードでピースを連続的に回したあと、指を離した時点で最寄りの向きへスナップする。
 * 呼び出し側は Three.js の Matrix4 / Quaternion から 3x3 を取り出して渡す想定なので、
 * 引数は行優先の 9 要素の数値配列とし、three の型には依存しない。
 */
export function nearestOrientation(m: readonly number[]): number {
  if (m.length !== 9) {
    throw new RangeError(`nearestOrientation: 行優先の 9 要素が必要 (got ${m.length})`);
  }
  for (const value of m) {
    if (typeof value !== 'number' || !Number.isFinite(value)) {
      throw new RangeError(`nearestOrientation: 有限の数値でない要素がある (${String(value)})`);
    }
  }

  let bestId = 0;
  let bestScore = Number.NEGATIVE_INFINITY;
  for (let id = 0; id < ORIENTATION_COUNT; id++) {
    const candidate = ORIENTATION_MATRICES[id];
    if (candidate === undefined) throw new Error(`向きの表に id ${id} が無い`);
    let score = 0;
    for (let i = 0; i < 9; i++) {
      score += (m[i] ?? 0) * (candidate[i] ?? 0);
    }
    // 厳密な > なので同点は先に見た（＝ id の小さい）方が残る
    if (score > bestScore) {
      bestScore = score;
      bestId = id;
    }
  }
  return bestId;
}
