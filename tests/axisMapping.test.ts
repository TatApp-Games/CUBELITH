import { describe, expect, it } from 'vitest';
import { vec3, type Axis } from '../src/core/grid';
import { axisStepVector, dragAxes } from '../src/input/axisMapping';

/** カメラの基底（右・上・前）を作る。前は右 × 上 の外積ではなく明示で渡す。 */
describe('dragAxes', () => {
  it('軸に揃ったカメラでは、右 / 上 / 奥がそのままの軸になる', () => {
    // +Z から原点を見るカメラ: 右 = +X、上 = +Y、前 = -Z
    const axes = dragAxes(vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, -1));
    expect(axes.right).toEqual({ axis: 'x', sign: 1 });
    expect(axes.up).toEqual({ axis: 'y', sign: 1 });
    expect(axes.depth).toEqual({ axis: 'z', sign: -1 });
  });

  it('反対側から見ると右と奥の符号が反転する', () => {
    // -Z から原点を見るカメラ: 右 = -X、上 = +Y、前 = +Z
    const axes = dragAxes(vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1));
    expect(axes.right).toEqual({ axis: 'x', sign: -1 });
    expect(axes.up).toEqual({ axis: 'y', sign: 1 });
    expect(axes.depth).toEqual({ axis: 'z', sign: 1 });
  });

  it('斜めのカメラでは最も近い軸が選ばれる', () => {
    // 方位角 30 度ほど回したカメラ。右は X 寄り、前は -Z 寄り
    const axes = dragAxes(vec3(0.87, 0, -0.5), vec3(0, 1, 0), vec3(-0.5, -0.2, -0.84));
    expect(axes.right.axis).toBe('x');
    expect(axes.right.sign).toBe(1);
    expect(axes.up).toEqual({ axis: 'y', sign: 1 });
    expect(axes.depth).toEqual({ axis: 'z', sign: -1 });
  });

  it('見下ろすカメラでは上方向が水平軸に割り当たる', () => {
    // ほぼ真上から見下ろす: 上ベクトルは -Z 寄り、前は -Y 寄り
    const axes = dragAxes(vec3(1, 0, 0), vec3(0, 0.1, -0.99), vec3(0, -0.99, -0.1));
    expect(axes.right).toEqual({ axis: 'x', sign: 1 });
    expect(axes.up).toEqual({ axis: 'z', sign: -1 });
    expect(axes.depth).toEqual({ axis: 'y', sign: -1 });
  });

  it('どんな向きでも 3 軸が重ならない', () => {
    const samples: readonly { right: ReturnType<typeof vec3>; up: ReturnType<typeof vec3>; forward: ReturnType<typeof vec3> }[] = [
      { right: vec3(1, 0, 0), up: vec3(0, 1, 0), forward: vec3(0, 0, -1) },
      { right: vec3(0.7, 0.7, 0), up: vec3(-0.7, 0.7, 0), forward: vec3(0, 0, -1) },
      { right: vec3(0.6, 0.5, 0.62), up: vec3(-0.3, 0.86, -0.4), forward: vec3(-0.74, 0, 0.67) },
      // 右と上が同じ軸を向く縮退した入力でも 3 軸に割り当てる
      { right: vec3(1, 0, 0), up: vec3(1, 0, 0), forward: vec3(1, 0, 0) },
      { right: vec3(0, 0, 0), up: vec3(0, 0, 0), forward: vec3(0, 0, 0) },
    ];
    for (const sample of samples) {
      const axes = dragAxes(sample.right, sample.up, sample.forward);
      const used: Axis[] = [axes.right.axis, axes.up.axis, axes.depth.axis];
      expect(new Set(used).size).toBe(3);
    }
  });
});

describe('axisStepVector', () => {
  it('軸と符号どおりのマス数を返す', () => {
    expect(axisStepVector({ axis: 'x', sign: 1 }, 2)).toEqual(vec3(2, 0, 0));
    expect(axisStepVector({ axis: 'y', sign: -1 }, 3)).toEqual(vec3(0, -3, 0));
    expect(axisStepVector({ axis: 'z', sign: -1 }, -1)).toEqual(vec3(0, 0, 1));
    expect(axisStepVector({ axis: 'z', sign: 1 }, 0)).toEqual(vec3(0, 0, 0));
  });
});
