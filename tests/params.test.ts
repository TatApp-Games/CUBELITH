// URL クエリの解釈（src/ui/params.ts）。`?seed=` の指定と不正値のフォールバックを確かめる。

import { describe, expect, it } from 'vitest';
import {
  clampInt,
  MAX_SEED,
  parseBoolParam,
  parseIntParam,
  parseSeedParam,
  readAppParams,
  withSettings,
} from '../src/ui/params';

describe('parseIntParam', () => {
  it('10 進整数を読む', () => {
    expect(parseIntParam('0')).toBe(0);
    expect(parseIntParam('42')).toBe(42);
    expect(parseIntParam('-7')).toBe(-7);
    expect(parseIntParam('+7')).toBe(7);
    expect(parseIntParam(' 12 ')).toBe(12);
  });

  it('整数でない値は undefined', () => {
    for (const raw of ['', ' ', 'abc', '1.5', '12abc', '0x10', '1e3', 'NaN', 'Infinity']) {
      expect(parseIntParam(raw), raw).toBeUndefined();
    }
  });

  it('null / undefined は undefined', () => {
    expect(parseIntParam(null)).toBeUndefined();
    expect(parseIntParam(undefined)).toBeUndefined();
  });

  it('安全な整数の範囲を超える値は undefined', () => {
    expect(parseIntParam('9007199254740993')).toBeUndefined();
  });
});

describe('parseBoolParam', () => {
  it('真偽を表す語を読む', () => {
    for (const raw of ['1', 'true', 'TRUE', 'on', 'yes']) {
      expect(parseBoolParam(raw), raw).toBe(true);
    }
    for (const raw of ['0', 'false', 'off', 'no']) {
      expect(parseBoolParam(raw), raw).toBe(false);
    }
  });

  it('それ以外は undefined', () => {
    for (const raw of ['', 'maybe', '2', null, undefined]) {
      expect(parseBoolParam(raw)).toBeUndefined();
    }
  });
});

describe('parseSeedParam', () => {
  it('符号なし 32 bit の範囲の整数を読む', () => {
    expect(parseSeedParam('0')).toBe(0);
    expect(parseSeedParam('12345')).toBe(12345);
    expect(parseSeedParam(String(MAX_SEED))).toBe(MAX_SEED);
  });

  it('範囲外は丸めずに捨てる', () => {
    expect(parseSeedParam('-1')).toBeUndefined();
    expect(parseSeedParam(String(MAX_SEED + 1))).toBeUndefined();
  });

  it('不正な値は undefined', () => {
    expect(parseSeedParam('abc')).toBeUndefined();
    expect(parseSeedParam('')).toBeUndefined();
  });
});

describe('clampInt', () => {
  it('範囲に丸める', () => {
    expect(clampInt(1, 3, 7)).toBe(3);
    expect(clampInt(9, 3, 7)).toBe(7);
    expect(clampInt(5, 3, 7)).toBe(5);
  });

  it('undefined はそのまま', () => {
    expect(clampInt(undefined, 3, 7)).toBeUndefined();
  });
});

describe('readAppParams', () => {
  it('指定が無ければすべて undefined（fps だけ false）', () => {
    expect(readAppParams('')).toEqual({
      n: undefined,
      m: undefined,
      seed: undefined,
      lite: undefined,
      fps: false,
    });
  });

  it('?seed= を読む', () => {
    expect(readAppParams('?seed=777').seed).toBe(777);
    expect(readAppParams('seed=777').seed).toBe(777);
  });

  it('不正な ?seed= は無視して既定動作へ落とす', () => {
    expect(readAppParams('?seed=').seed).toBeUndefined();
    expect(readAppParams('?seed=abc').seed).toBeUndefined();
    expect(readAppParams('?seed=1.5').seed).toBeUndefined();
    expect(readAppParams('?seed=-3').seed).toBeUndefined();
  });

  it('n / m / lite / fps も読む', () => {
    const params = readAppParams('?n=5&m=12&lite=1&fps=1');
    expect(params).toEqual({ n: 5, m: 12, seed: undefined, lite: true, fps: true });
  });

  it('不正な n / m は無視する', () => {
    const params = readAppParams('?n=big&m=&seed=9');
    expect(params.n).toBeUndefined();
    expect(params.m).toBeUndefined();
    expect(params.seed).toBe(9);
  });
});

describe('withSettings', () => {
  it('n / m / seed を差し替える', () => {
    expect(withSettings('?seed=1&n=3&m=4', { n: 5, m: 9, seed: 42 })).toBe(
      '?seed=42&n=5&m=9',
    );
  });

  it('他の指定は残す', () => {
    const next = withSettings('?lite=1&fps=1', { n: 3, m: 4, seed: 7 });
    const params = new URLSearchParams(next);
    expect(params.get('lite')).toBe('1');
    expect(params.get('fps')).toBe('1');
    expect(params.get('seed')).toBe('7');
  });

  it('空のクエリからも作れる', () => {
    expect(withSettings('', { n: 3, m: 4, seed: 0 })).toBe('?n=3&m=4&seed=0');
  });
});
