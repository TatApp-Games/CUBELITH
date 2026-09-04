import { describe, expect, it } from 'vitest';
import { pickSampleOffsets } from '../src/input/pickSamples';

/** 原点からの距離。リングの半径どおりかを見るのに使う。 */
function length(offset: { dx: number; dy: number }): number {
  return Math.hypot(offset.dx, offset.dy);
}

describe('pickSampleOffsets', () => {
  it('先頭は必ず中心 (0, 0)', () => {
    expect(pickSampleOffsets(16)[0]).toEqual({ dx: 0, dy: 0 });
    expect(pickSampleOffsets(8, 4, 1)[0]).toEqual({ dx: 0, dy: 0 });
    expect(pickSampleOffsets(0)[0]).toEqual({ dx: 0, dy: 0 });
  });

  it('点数とリング本数どおりの個数を返す', () => {
    expect(pickSampleOffsets(16, 6, 2)).toHaveLength(1 + 6 * 2);
    expect(pickSampleOffsets(16, 8, 1)).toHaveLength(1 + 8);
    expect(pickSampleOffsets(16, 1, 3)).toHaveLength(1 + 3);
  });

  it('既定のサンプル数は十数本に収まる', () => {
    expect(pickSampleOffsets(16).length).toBeLessThanOrEqual(16);
  });

  it('リングは内側から順に並び、半径は radius * i / rings', () => {
    const offsets = pickSampleOffsets(12, 4, 2);
    // 中心を除いた 8 点のうち、前半 4 点が半径 6、後半 4 点が半径 12
    for (let i = 1; i <= 4; i += 1) expect(length(offsets[i]!)).toBeCloseTo(6);
    for (let i = 5; i <= 8; i += 1) expect(length(offsets[i]!)).toBeCloseTo(12);
  });

  it('1 リングなら指定した半径の円周上に等間隔で並ぶ', () => {
    const offsets = pickSampleOffsets(10, 4, 1);
    expect(offsets).toHaveLength(5);
    expect(offsets[1]!.dx).toBeCloseTo(10);
    expect(offsets[1]!.dy).toBeCloseTo(0);
    expect(offsets[2]!.dx).toBeCloseTo(0);
    expect(offsets[2]!.dy).toBeCloseTo(10);
    expect(offsets[3]!.dx).toBeCloseTo(-10);
    expect(offsets[3]!.dy).toBeCloseTo(0);
    expect(offsets[4]!.dx).toBeCloseTo(0);
    expect(offsets[4]!.dy).toBeCloseTo(-10);
  });

  it('リングごとに半ステップずらして方角が重ならない', () => {
    const offsets = pickSampleOffsets(12, 4, 2);
    const angleOf = (o: { dx: number; dy: number }): number => Math.atan2(o.dy, o.dx);
    // 内側 1 点目は 0 度、外側 1 点目は 1 ステップ（90 度）の半分 = 45 度
    expect(angleOf(offsets[1]!)).toBeCloseTo(0);
    expect(angleOf(offsets[5]!)).toBeCloseTo(Math.PI / 4);
  });

  it('半径 0 や不正値では中心 1 点だけを返す', () => {
    expect(pickSampleOffsets(0)).toEqual([{ dx: 0, dy: 0 }]);
    expect(pickSampleOffsets(-4)).toEqual([{ dx: 0, dy: 0 }]);
    expect(pickSampleOffsets(Number.NaN)).toEqual([{ dx: 0, dy: 0 }]);
    expect(pickSampleOffsets(Number.POSITIVE_INFINITY)).toEqual([{ dx: 0, dy: 0 }]);
  });

  it('点数 / リング本数が不正でも中心 1 点にフォールバックする', () => {
    expect(pickSampleOffsets(16, 0, 2)).toEqual([{ dx: 0, dy: 0 }]);
    expect(pickSampleOffsets(16, 6, 0)).toEqual([{ dx: 0, dy: 0 }]);
    expect(pickSampleOffsets(16, -3, 2)).toEqual([{ dx: 0, dy: 0 }]);
    expect(pickSampleOffsets(16, Number.NaN, 2)).toEqual([{ dx: 0, dy: 0 }]);
    expect(pickSampleOffsets(16, 6, Number.NaN)).toEqual([{ dx: 0, dy: 0 }]);
  });

  it('小数の点数は切り捨てて扱う', () => {
    expect(pickSampleOffsets(16, 4.9, 1)).toHaveLength(1 + 4);
  });
});
