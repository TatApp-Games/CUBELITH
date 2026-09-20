// 2 本指ジェスチャの認識（src/input/twoFingerGesture.ts）。
// ピンチ / ひねり / 平行スワイプの切り分けと、「回転は 1 回だけ」を確かめる。

import { describe, expect, it } from 'vitest';
import {
  createTwoFingerGesture,
  PINCH_RATIO_THRESHOLD,
  SWIPE_PIXELS_THRESHOLD,
  TWIST_RADIANS_THRESHOLD,
  type Point,
  type TwoFingerAction,
} from '../src/input/twoFingerGesture';

/** 中点 (cx, cy)・間隔 gap・傾き angle の 2 本指。 */
function fingers(cx: number, cy: number, gap: number, angle = 0): [Point, Point] {
  const hx = (Math.cos(angle) * gap) / 2;
  const hy = (Math.sin(angle) * gap) / 2;
  return [
    { x: cx - hx, y: cy - hy },
    { x: cx + hx, y: cy + hy },
  ];
}

/** reset してから 1 回だけ update した結果。 */
function once(start: [Point, Point], next: [Point, Point]): TwoFingerAction {
  const gesture = createTwoFingerGesture();
  gesture.reset(start[0], start[1]);
  return gesture.update(next[0], next[1]);
}

describe('createTwoFingerGesture', () => {
  it('動いていなければ何も起きない', () => {
    const start = fingers(200, 300, 100);
    expect(once(start, start)).toEqual({ kind: 'none' });
  });

  it('閾値未満の動きでは何も起きない', () => {
    const start = fingers(200, 300, 100);
    const next = fingers(200 + SWIPE_PIXELS_THRESHOLD * 0.5, 300, 100);
    expect(once(start, next)).toEqual({ kind: 'none' });
  });

  it('指を広げるとズーム（半径が縮む = 寄る）', () => {
    const start = fingers(200, 300, 100);
    const next = fingers(200, 300, 100 * (1 + PINCH_RATIO_THRESHOLD * 2));
    const action = once(start, next);
    expect(action.kind).toBe('zoom');
    if (action.kind !== 'zoom') throw new Error('zoom ではない');
    expect(action.scale).toBeLessThan(1);
  });

  it('指を狭めると半径が伸びる = 引く', () => {
    const start = fingers(200, 300, 100);
    const next = fingers(200, 300, 100 * (1 - PINCH_RATIO_THRESHOLD * 2));
    const action = once(start, next);
    expect(action.kind).toBe('zoom');
    if (action.kind !== 'zoom') throw new Error('zoom ではない');
    expect(action.scale).toBeGreaterThan(1);
  });

  it('右への平行スワイプは Yaw +', () => {
    const start = fingers(200, 300, 100);
    const next = fingers(200 + SWIPE_PIXELS_THRESHOLD * 2, 300, 100);
    expect(once(start, next)).toEqual({ kind: 'rotate', gesture: 'yaw', dir: 1 });
  });

  it('左への平行スワイプは Yaw −', () => {
    const start = fingers(200, 300, 100);
    const next = fingers(200 - SWIPE_PIXELS_THRESHOLD * 2, 300, 100);
    expect(once(start, next)).toEqual({ kind: 'rotate', gesture: 'yaw', dir: -1 });
  });

  it('上への平行スワイプは Pitch +（画面座標は Y が下向き）', () => {
    const start = fingers(200, 300, 100);
    const next = fingers(200, 300 - SWIPE_PIXELS_THRESHOLD * 2, 100);
    expect(once(start, next)).toEqual({ kind: 'rotate', gesture: 'pitch', dir: 1 });
  });

  it('下への平行スワイプは Pitch −', () => {
    const start = fingers(200, 300, 100);
    const next = fingers(200, 300 + SWIPE_PIXELS_THRESHOLD * 2, 100);
    expect(once(start, next)).toEqual({ kind: 'rotate', gesture: 'pitch', dir: -1 });
  });

  it('時計回りのひねりは Roll +', () => {
    const start = fingers(200, 300, 100, 0);
    const next = fingers(200, 300, 100, TWIST_RADIANS_THRESHOLD * 2);
    expect(once(start, next)).toEqual({ kind: 'rotate', gesture: 'roll', dir: 1 });
  });

  it('反時計回りのひねりは Roll −', () => {
    const start = fingers(200, 300, 100, 0);
    const next = fingers(200, 300, 100, -TWIST_RADIANS_THRESHOLD * 2);
    expect(once(start, next)).toEqual({ kind: 'rotate', gesture: 'roll', dir: -1 });
  });

  it('回転はひと続きのジェスチャで 1 回だけ（連続で回り続けない）', () => {
    const gesture = createTwoFingerGesture();
    const start = fingers(200, 300, 100);
    gesture.reset(start[0], start[1]);
    const first = gesture.update(...fingers(200 + SWIPE_PIXELS_THRESHOLD * 2, 300, 100));
    expect(first).toEqual({ kind: 'rotate', gesture: 'yaw', dir: 1 });
    for (let step = 3; step < 10; step += 1) {
      const next = fingers(200 + SWIPE_PIXELS_THRESHOLD * step, 300, 100);
      expect(gesture.update(next[0], next[1])).toEqual({ kind: 'none' });
    }
  });

  it('指を置き直せばまた回せる', () => {
    const gesture = createTwoFingerGesture();
    gesture.reset(...fingers(200, 300, 100));
    expect(gesture.update(...fingers(200 + SWIPE_PIXELS_THRESHOLD * 2, 300, 100)).kind).toBe(
      'rotate',
    );
    gesture.reset(...fingers(200, 300, 100));
    expect(gesture.update(...fingers(200 + SWIPE_PIXELS_THRESHOLD * 2, 300, 100)).kind).toBe(
      'rotate',
    );
  });

  it('ピンチと判定したらそのまま続き、途中でスワイプしても回らない', () => {
    const gesture = createTwoFingerGesture();
    gesture.reset(...fingers(200, 300, 100));
    expect(gesture.update(...fingers(200, 300, 130)).kind).toBe('zoom');
    // 大きく平行移動しても回転は出ない
    expect(gesture.update(...fingers(600, 300, 130)).kind).toBe('zoom');
  });

  it('ズームは前フレームからの変化を返すので、掛け合わせると全体の倍率になる', () => {
    const gesture = createTwoFingerGesture();
    gesture.reset(...fingers(200, 300, 100));
    let total = 1;
    for (const gap of [130, 160, 200]) {
      const action = gesture.update(...fingers(200, 300, gap));
      if (action.kind !== 'zoom') throw new Error('zoom ではない');
      total *= action.scale;
    }
    expect(total).toBeCloseTo(100 / 200, 10);
  });
});
