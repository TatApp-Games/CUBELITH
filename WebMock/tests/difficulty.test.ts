// 難易度の選択肢（src/ui/difficulty.ts）。N ごとの M のプリセットが SPEC.md 3.1 の表どおりかを確かめる。

import { describe, expect, it } from 'vitest';
import { maxPieces } from '../src/core/generate';
import { DEFAULT_PIECE_COUNT, DEFAULT_SPACE_SIZE, nearestPreset, piecePresets, spaceSizes } from '../src/ui/difficulty';

describe('piecePresets', () => {
  it('SPEC.md 3.1 の表に一致する', () => {
    expect(piecePresets(3)).toEqual([3, 4, 5, 6, 7]);
    expect(piecePresets(4)).toEqual([4, 6, 8, 10, 12]);
    expect(piecePresets(5)).toEqual([5, 8, 11, 14, 17]);
    expect(piecePresets(6)).toEqual([6, 10, 14, 18, 22]);
    expect(piecePresets(7)).toEqual([7, 12, 17, 22, 27]);
  });

  it('どの N でも最大の段は maxPieces(n)', () => {
    for (const n of spaceSizes()) expect(piecePresets(n).at(-1)).toBe(maxPieces(n));
  });

  it('既定の N に既定の M が含まれる', () => {
    expect(piecePresets(DEFAULT_SPACE_SIZE)).toContain(DEFAULT_PIECE_COUNT);
  });
});

describe('nearestPreset', () => {
  it('N を切り替えたとき最も近い段へ寄せる', () => {
    expect(nearestPreset(piecePresets(4), 7)).toBe(6);
    expect(nearestPreset(piecePresets(3), 2)).toBe(3);
    expect(nearestPreset(piecePresets(3), 40)).toBe(7);
  });
});
