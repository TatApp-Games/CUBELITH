// 画面のドラッグ方向を、カメラの向きに応じてワールドのグリッド軸へ写像する（SPEC.md 3.3「移動」）。
// カメラ依存の計算なので src/core には置かない。ただしここ自体は Three.js に依存しない純粋な関数で、
// 呼び出し側（pieceInput）がカメラの基底ベクトルを取り出して渡す。

import { vec3, type Axis, type Vec3 } from '../core/grid';

/** グリッド軸と符号の組。count マス動かすと axis 方向へ sign * count 進む。 */
export type AxisStep = { readonly axis: Axis; readonly sign: 1 | -1 };

/** ドラッグ 2 軸（画面の右 / 上）と、残る 1 軸（奥行き）の割り当て。 */
export type DragAxes = {
  /** 画面を右へ動かしたときに進む向き。 */
  readonly right: AxisStep;
  /** 画面を上へ動かしたときに進む向き。 */
  readonly up: AxisStep;
  /** 奥（カメラから遠ざかる側）へ進む向き。 */
  readonly depth: AxisStep;
};

const ALL_AXES: readonly Axis[] = ['x', 'y', 'z'];

/** ベクトルの軸成分。 */
function component(v: Vec3, axis: Axis): number {
  if (axis === 'x') return v.x;
  if (axis === 'y') return v.y;
  return v.z;
}

/** |成分| が最大の軸を選ぶ。exclude の軸は候補から外す。同点は x → y → z の順で決定的に選ぶ。 */
function dominantAxis(v: Vec3, exclude: readonly Axis[]): AxisStep {
  let best: Axis | undefined;
  let bestMagnitude = -1;
  for (const axis of ALL_AXES) {
    if (exclude.includes(axis)) continue;
    const magnitude = Math.abs(component(v, axis));
    if (magnitude > bestMagnitude) {
      best = axis;
      bestMagnitude = magnitude;
    }
  }
  if (best === undefined) throw new Error('軸の候補が残っていない');
  // 成分が 0 のときは向きを決められないので + を既定にする
  return { axis: best, sign: component(v, best) < 0 ? -1 : 1 };
}

/**
 * カメラの基底ベクトル（ワールド座標）からドラッグ軸の割り当てを作る。
 *
 * right / up / forward は正規化されていなくてよい（比較に使うのは成分の大小だけ）。
 * forward はカメラが見ている方向で、depth はそれに最も近い残りの軸になる。
 * right と up が同じ軸を向く縮退した場合でも 3 軸が重ならないよう、選んだ軸を順に除外していく。
 */
export function dragAxes(right: Vec3, up: Vec3, forward: Vec3): DragAxes {
  const rightStep = dominantAxis(right, []);
  const upStep = dominantAxis(up, [rightStep.axis]);
  const depthStep = dominantAxis(forward, [rightStep.axis, upStep.axis]);
  return { right: rightStep, up: upStep, depth: depthStep };
}

/** 軸ステップを count マス分の移動ベクトルにする。 */
export function axisStepVector(step: AxisStep, count: number): Vec3 {
  const value = step.sign * count;
  if (step.axis === 'x') return vec3(value, 0, 0);
  if (step.axis === 'y') return vec3(0, value, 0);
  return vec3(0, 0, value);
}
