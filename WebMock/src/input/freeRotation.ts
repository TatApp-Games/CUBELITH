// 回転モードの自由回転（Three.js のクォータニオン）を、90 度単位の向き id へ写す（RULES.md 3.3）。
// axisMapping.ts と同じく「画面 / カメラの都合を core の言葉へ翻訳する」層で、
// core は Three.js に依存できない（CLAUDE.md 開発ルール 1）ので、
// 列優先 → 行優先の並べ替えはここで済ませ、core へは数値配列だけを渡す。

import * as THREE from 'three';
import { nearestOrientation, orientationMatrix } from '../core/grid';

// 変換に使う一時行列。指を離したときにしか通らないが、毎回作る理由も無いので使い回す
const logicalMatrix = new THREE.Matrix4();
const freeMatrix = new THREE.Matrix4();
const composedMatrix = new THREE.Matrix4();

/**
 * three の Matrix4 の左上 3x3 を、core が受け取る行優先 9 要素の配列にする。
 *
 * `Matrix4.elements` は**列優先**（elements[列 * 4 + 行]）なので、
 * 行優先 [n11, n12, n13, n21, n22, n23, n31, n32, n33] は
 * [e0, e4, e8, e1, e5, e9, e2, e6, e10] の順で拾う。
 */
export function rowMajor3x3(matrix: THREE.Matrix4): number[] {
  const e = matrix.elements;
  return [e[0], e[4], e[8], e[1], e[5], e[9], e[2], e[6], e[10]];
}

/**
 * 今の向き orientation のピースに自由回転 quaternion を掛けた姿勢に、最も近い向き id を返す。
 *
 * 表示は「向き M で置いたピースを、あとから q で回したもの」（render/pieces.ts の setFreeRotation）
 * なので、合成は左から掛けた q·M になる。`Matrix4.set` の引数は行優先なので、
 * `orientationMatrix` の行優先 9 要素をそのまま並べて載せられる。
 */
export function snappedOrientation(orientation: number, quaternion: THREE.Quaternion): number {
  const m = orientationMatrix(orientation);
  logicalMatrix.set(
    m[0], m[1], m[2], 0,
    m[3], m[4], m[5], 0,
    m[6], m[7], m[8], 0,
    0, 0, 0, 1,
  );
  freeMatrix.makeRotationFromQuaternion(quaternion);
  composedMatrix.multiplyMatrices(freeMatrix, logicalMatrix);
  return nearestOrientation(rowMajor3x3(composedMatrix));
}
