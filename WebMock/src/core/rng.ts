// シード付き乱数（mulberry32）。生成結果の再現に使う。Three.js に依存しない。

export type Rng = {
  /** [0, 1) の一様乱数 */
  next(): number;
  /** [0, n) の整数 */
  nextInt(n: number): number;
};

export function createRng(seed: number): Rng {
  let a = seed >>> 0;
  const next = (): number => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  return {
    next,
    nextInt(n: number): number {
      if (!Number.isInteger(n) || n <= 0) throw new RangeError(`nextInt: n は正の整数 (got ${n})`);
      return Math.floor(next() * n);
    },
  };
}
