import { describe, expect, it } from 'vitest';
import {
  addVec3,
  compareVec3,
  composeOrientation,
  equalsVec3,
  IDENTITY_ORIENTATION,
  ORIENTATION_COUNT,
  orientationMatrix,
  rotateOrientation,
  rotateVoxel,
  subVec3,
  vec3,
  vec3Key,
  type Axis,
  type Mat3,
  type Vec3,
} from '../src/core/grid';

const ALL_ORIENTATIONS: readonly number[] = Array.from({ length: ORIENTATION_COUNT }, (_, i) => i);
const AXES: readonly Axis[] = ['x', 'y', 'z'];
const DIRECTIONS: readonly (1 | -1)[] = [1, -1];

/** 3x3 行列式（テスト側で独立に計算する） */
function determinant(m: Mat3): number {
  const [a, b, c, d, e, f, g, h, i] = m;
  return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

/** ボクセル集合の全ペアの距離の 2 乗（昇順）。回転で不変なはず。 */
function pairwiseSquaredDistances(voxels: readonly Vec3[]): number[] {
  const distances: number[] = [];
  for (let i = 0; i < voxels.length; i++) {
    for (let j = i + 1; j < voxels.length; j++) {
      const a = voxels[i];
      const b = voxels[j];
      if (a === undefined || b === undefined) continue;
      const d = subVec3(a, b);
      distances.push(d.x * d.x + d.y * d.y + d.z * d.z);
    }
  }
  return distances.sort((p, q) => p - q);
}

/** 検証用のサンプルピース（L 字のテトロミノ）。対称性が低く回転の違いが出る。 */
const SAMPLE_VOXELS: readonly Vec3[] = [vec3(0, 0, 0), vec3(1, 0, 0), vec3(2, 0, 0), vec3(2, 1, 0)];

describe('Vec3 のユーティリティ', () => {
  it('vec3 / addVec3 / subVec3 が成分ごとに働く', () => {
    expect(vec3(1, 2, 3)).toEqual({ x: 1, y: 2, z: 3 });
    expect(addVec3(vec3(1, 2, 3), vec3(-4, 5, 6))).toEqual({ x: -3, y: 7, z: 9 });
    expect(subVec3(vec3(1, 2, 3), vec3(-4, 5, 6))).toEqual({ x: 5, y: -3, z: -3 });
  });

  it('equalsVec3 は全成分が一致したときだけ真', () => {
    expect(equalsVec3(vec3(1, 2, 3), vec3(1, 2, 3))).toBe(true);
    expect(equalsVec3(vec3(1, 2, 3), vec3(1, 2, 4))).toBe(false);
    expect(equalsVec3(vec3(1, 2, 3), vec3(0, 2, 3))).toBe(false);
    expect(equalsVec3(vec3(1, 2, 3), vec3(1, 0, 3))).toBe(false);
  });

  it('compareVec3 は x → y → z の辞書順', () => {
    expect(compareVec3(vec3(0, 0, 0), vec3(1, 0, 0))).toBeLessThan(0);
    expect(compareVec3(vec3(0, 5, 0), vec3(0, 1, 9))).toBeGreaterThan(0);
    expect(compareVec3(vec3(0, 0, 2), vec3(0, 0, 3))).toBeLessThan(0);
    expect(compareVec3(vec3(2, 3, 4), vec3(2, 3, 4))).toBe(0);

    const sorted = [vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 0, 0)].sort(compareVec3);
    expect(sorted).toEqual([vec3(0, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0), vec3(1, 0, 0)]);
  });

  it('vec3Key は座標が同じときだけ一致する', () => {
    expect(vec3Key(vec3(1, -2, 3))).toBe(vec3Key(vec3(1, -2, 3)));
    expect(vec3Key(vec3(1, -2, 3))).not.toBe(vec3Key(vec3(1, 2, 3)));
    expect(new Set([vec3(1, 1, 1), vec3(1, 1, 1), vec3(1, 1, 2)].map(vec3Key)).size).toBe(2);
  });
});

describe('向きの表', () => {
  it('ORIENTATION_COUNT は 24、恒等の id は 0', () => {
    expect(ORIENTATION_COUNT).toBe(24);
    expect(IDENTITY_ORIENTATION).toBe(0);
    expect(orientationMatrix(IDENTITY_ORIENTATION)).toEqual([1, 0, 0, 0, 1, 0, 0, 0, 1]);
  });

  it('24 通りの回転行列が相異なる', () => {
    const keys = new Set(ALL_ORIENTATIONS.map((o) => orientationMatrix(o).join(',')));
    expect(keys.size).toBe(ORIENTATION_COUNT);
  });

  it('24 通りの向きは代表点の変換結果でも相異なる', () => {
    // 各成分の絶対値が相異なる点なので、符号付き置換 24 通りは必ず別の点に写る
    const probe = vec3(1, 2, 3);
    const results = new Set(ALL_ORIENTATIONS.map((o) => vec3Key(rotateVoxel(probe, o))));
    expect(results.size).toBe(ORIENTATION_COUNT);
  });

  it('すべて行列式 +1 の整数行列（鏡像を含まない）', () => {
    for (const o of ALL_ORIENTATIONS) {
      const m = orientationMatrix(o);
      expect(m).toHaveLength(9);
      for (const value of m) expect(Number.isInteger(value)).toBe(true);
      expect(determinant(m)).toBe(1);
    }
  });

  it('範囲外や非整数の向き id は RangeError', () => {
    expect(() => orientationMatrix(-1)).toThrow(RangeError);
    expect(() => orientationMatrix(ORIENTATION_COUNT)).toThrow(RangeError);
    expect(() => orientationMatrix(1.5)).toThrow(RangeError);
    expect(() => orientationMatrix(Number.NaN)).toThrow(RangeError);
    expect(() => composeOrientation(0, 24)).toThrow(RangeError);
    expect(() => rotateVoxel(vec3(0, 0, 0), 99)).toThrow(RangeError);
  });
});

describe('rotateVoxel', () => {
  it('恒等の向きでは座標が変わらない', () => {
    for (const v of SAMPLE_VOXELS) {
      expect(rotateVoxel(v, IDENTITY_ORIENTATION)).toEqual(v);
    }
  });

  it('各軸の +90 度回転が右手系の期待値になる', () => {
    const x = rotateOrientation(IDENTITY_ORIENTATION, 'x', 1);
    const y = rotateOrientation(IDENTITY_ORIENTATION, 'y', 1);
    const z = rotateOrientation(IDENTITY_ORIENTATION, 'z', 1);
    // Rx: (x, y, z) -> (x, -z, y)
    expect(rotateVoxel(vec3(1, 2, 3), x)).toEqual(vec3(1, -3, 2));
    // Ry: (x, y, z) -> (z, y, -x)
    expect(rotateVoxel(vec3(1, 2, 3), y)).toEqual(vec3(3, 2, -1));
    // Rz: (x, y, z) -> (-y, x, z)
    expect(rotateVoxel(vec3(1, 2, 3), z)).toEqual(vec3(-2, 1, 3));
  });

  it('整数座標のまま、ボクセル数と互いの距離を保つ', () => {
    const originalPairs = pairwiseSquaredDistances(SAMPLE_VOXELS);
    for (const o of ALL_ORIENTATIONS) {
      const rotated = SAMPLE_VOXELS.map((v) => rotateVoxel(v, o));
      for (const v of rotated) {
        expect(Number.isInteger(v.x)).toBe(true);
        expect(Number.isInteger(v.y)).toBe(true);
        expect(Number.isInteger(v.z)).toBe(true);
      }
      expect(new Set(rotated.map(vec3Key)).size).toBe(SAMPLE_VOXELS.length);
      expect(pairwiseSquaredDistances(rotated)).toEqual(originalPairs);
    }
  });

  it('原点は動かない（回転はピースの局所原点まわり）', () => {
    for (const o of ALL_ORIENTATIONS) {
      expect(rotateVoxel(vec3(0, 0, 0), o)).toEqual(vec3(0, 0, 0));
    }
  });
});

describe('向きの合成', () => {
  it('恒等の id が単位元として働く', () => {
    for (const o of ALL_ORIENTATIONS) {
      expect(composeOrientation(o, IDENTITY_ORIENTATION)).toBe(o);
      expect(composeOrientation(IDENTITY_ORIENTATION, o)).toBe(o);
    }
  });

  it('結合的である', () => {
    for (const a of ALL_ORIENTATIONS) {
      for (const b of ALL_ORIENTATIONS) {
        for (const c of ALL_ORIENTATIONS) {
          expect(composeOrientation(composeOrientation(a, b), c)).toBe(
            composeOrientation(a, composeOrientation(b, c)),
          );
        }
      }
    }
  });

  it('合成は「a を適用してから b を適用する」と一致する', () => {
    const probe = vec3(1, 2, 3);
    for (const a of ALL_ORIENTATIONS) {
      for (const b of ALL_ORIENTATIONS) {
        const composed = rotateVoxel(probe, composeOrientation(a, b));
        const stepwise = rotateVoxel(rotateVoxel(probe, a), b);
        expect(composed).toEqual(stepwise);
      }
    }
  });

  it('合成の結果は 24 通りの中で閉じている', () => {
    for (const a of ALL_ORIENTATIONS) {
      // 群なので、1 つの a に対する合成結果は 24 通りを 1 度ずつ取る
      const row = new Set(ALL_ORIENTATIONS.map((b) => composeOrientation(a, b)));
      expect(row.size).toBe(ORIENTATION_COUNT);
      for (const id of row) {
        expect(Number.isInteger(id)).toBe(true);
        expect(id).toBeGreaterThanOrEqual(0);
        expect(id).toBeLessThan(ORIENTATION_COUNT);
      }
    }
  });
});

describe('rotateOrientation', () => {
  it('同じ軸で 4 回 90 度回すと元の向きに戻る（全 24 向き × 3 軸 × ±）', () => {
    for (const start of ALL_ORIENTATIONS) {
      for (const axis of AXES) {
        for (const dir of DIRECTIONS) {
          let current = start;
          for (let step = 0; step < 4; step++) {
            current = rotateOrientation(current, axis, dir);
            if (step < 3) expect(current).not.toBe(start);
          }
          expect(current).toBe(start);
        }
      }
    }
  });

  it('+1 と -1 は互いに打ち消す', () => {
    for (const start of ALL_ORIENTATIONS) {
      for (const axis of AXES) {
        expect(rotateOrientation(rotateOrientation(start, axis, 1), axis, -1)).toBe(start);
        expect(rotateOrientation(rotateOrientation(start, axis, -1), axis, 1)).toBe(start);
      }
    }
  });

  it('-1 は +1 を 3 回繰り返したものと同じ', () => {
    for (const start of ALL_ORIENTATIONS) {
      for (const axis of AXES) {
        let thrice = start;
        for (let i = 0; i < 3; i++) thrice = rotateOrientation(thrice, axis, 1);
        expect(rotateOrientation(start, axis, -1)).toBe(thrice);
      }
    }
  });

  it('恒等から 3 軸の回転を組み合わせると 24 通りすべてに到達できる', () => {
    const reached = new Set<number>([IDENTITY_ORIENTATION]);
    const queue: number[] = [IDENTITY_ORIENTATION];
    for (let head = 0; head < queue.length; head++) {
      const current = queue[head];
      if (current === undefined) continue;
      for (const axis of AXES) {
        for (const dir of DIRECTIONS) {
          const next = rotateOrientation(current, axis, dir);
          if (reached.has(next)) continue;
          reached.add(next);
          queue.push(next);
        }
      }
    }
    expect(reached.size).toBe(ORIENTATION_COUNT);
  });
});
