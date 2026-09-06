// 回転モードの「指を離したら最寄りの 90 度へスナップする」計算のテスト。
// 列優先（three）と行優先（core）の取り違えが最も起きやすいところなので、
// 恒等・90 度・24 通りの全向きで結果が合うことを確かめる。

import * as THREE from 'three';
import { describe, expect, it } from 'vitest';
import { rowMajor3x3, snappedOrientation } from '../src/input/freeRotation';
import {
  composeOrientation,
  IDENTITY_ORIENTATION,
  ORIENTATION_COUNT,
  orientationMatrix,
  rotateOrientation,
  type Axis,
} from '../src/core/grid';

/** 0..23 の全向き。 */
const ALL_ORIENTATIONS: readonly number[] = Array.from(
  { length: ORIENTATION_COUNT },
  (_, id): number => id,
);

/** 軸まわりに degrees 度回すクォータニオン。core の ROTATION_* と同じ右手系。 */
function quaternionAround(axis: Axis, degrees: number): THREE.Quaternion {
  const vector = new THREE.Vector3(axis === 'x' ? 1 : 0, axis === 'y' ? 1 : 0, axis === 'z' ? 1 : 0);
  return new THREE.Quaternion().setFromAxisAngle(vector, THREE.MathUtils.degToRad(degrees));
}

describe('rowMajor3x3', () => {
  it('単位行列は行優先でも単位行列', () => {
    expect(rowMajor3x3(new THREE.Matrix4())).toEqual([1, 0, 0, 0, 1, 0, 0, 0, 1]);
  });

  it('Matrix4.set（行優先の引数）と同じ並びで取り出せる', () => {
    // 列優先の elements と取り違えていれば転置になって落ちる
    const matrix = new THREE.Matrix4().set(1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0, 0, 0, 0, 1);
    expect(rowMajor3x3(matrix)).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9]);
  });

  it('Y 軸 +90 度のクォータニオンは core の Yaw + と同じ行列になる', () => {
    const matrix = new THREE.Matrix4().makeRotationFromQuaternion(quaternionAround('y', 90));
    const expected = orientationMatrix(rotateOrientation(IDENTITY_ORIENTATION, 'y', 1));
    rowMajor3x3(matrix).forEach((value, i): void => {
      expect(value).toBeCloseTo(expected[i] ?? 0, 10);
    });
  });
});

describe('snappedOrientation', () => {
  it('自由回転が恒等なら向きは変わらない', () => {
    const identity = new THREE.Quaternion();
    for (const id of ALL_ORIENTATIONS) {
      expect(snappedOrientation(id, identity)).toBe(id);
    }
  });

  it('恒等の向きに軸 ±90 度を掛けると core の rotateOrientation と一致する', () => {
    const axes: readonly Axis[] = ['x', 'y', 'z'];
    for (const axis of axes) {
      expect(snappedOrientation(IDENTITY_ORIENTATION, quaternionAround(axis, 90))).toBe(
        rotateOrientation(IDENTITY_ORIENTATION, axis, 1),
      );
      expect(snappedOrientation(IDENTITY_ORIENTATION, quaternionAround(axis, -90))).toBe(
        rotateOrientation(IDENTITY_ORIENTATION, axis, -1),
      );
    }
  });

  it('どの向きからでも軸 90 度は「今の向きのあとにワールド軸で回す」合成になる', () => {
    const axes: readonly Axis[] = ['x', 'y', 'z'];
    for (const id of ALL_ORIENTATIONS) {
      for (const axis of axes) {
        expect(snappedOrientation(id, quaternionAround(axis, 90))).toBe(
          rotateOrientation(id, axis, 1),
        );
      }
    }
  });

  it('45 度未満のずれは元の向きへ、45 度を超えると次の向きへスナップする', () => {
    for (const id of ALL_ORIENTATIONS) {
      expect(snappedOrientation(id, quaternionAround('y', 20))).toBe(id);
      expect(snappedOrientation(id, quaternionAround('y', -20))).toBe(id);
      expect(snappedOrientation(id, quaternionAround('y', 80))).toBe(rotateOrientation(id, 'y', 1));
      expect(snappedOrientation(id, quaternionAround('y', 100))).toBe(rotateOrientation(id, 'y', 1));
    }
  });

  it('2 軸ぶんのトラックボール回転も 24 通りのどれかへ落ちる', () => {
    // 右へ 90 度・上へ 90 度ぶん回したときの合成（掛ける順は表示と同じく左から）
    const yaw = quaternionAround('y', 88);
    const pitch = quaternionAround('x', -92);
    const combined = new THREE.Quaternion().multiplyQuaternions(yaw, pitch);
    const result = snappedOrientation(IDENTITY_ORIENTATION, combined);
    expect(result).toBe(
      composeOrientation(
        rotateOrientation(IDENTITY_ORIENTATION, 'x', -1),
        // composeOrientation(a, b) は「a を適用してから b」。ワールド軸の回転を重ねる順と同じ
        rotateOrientation(IDENTITY_ORIENTATION, 'y', 1),
      ),
    );
  });
});
