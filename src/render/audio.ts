// 吸い付いた瞬間の効果音（SPEC.md 3.5 / 10 章）。
// 音源ファイルは作らず WebAudio の合成音で代用する。ブラウザ API に依存するので src/core には置かない。

/**
 * 「ガラスが組み合う」音の作り。
 * 高めの基音に少しずれた倍音を重ね、10 ms で立ち上げて一気に減衰させると硬い「カチッ」になる。
 */
const BASE_FREQUENCY = 1180;
const PARTIAL_RATIOS: readonly number[] = [1, 1.5, 2.02];
/** 減衰までの長さ（秒）。短くないと「吸い付いた」感じにならない。 */
const DURATION = 0.18;
/** 基音のピーク音量。倍音はこれを分割して重ねる。 */
const PEAK_GAIN = 0.16;
/** 立ち上がりの長さ（秒）。 */
const ATTACK = 0.006;
/** exponentialRamp は 0 を扱えないので、無音の代わりに使う微小値。 */
const SILENCE = 0.0001;

export type SnapAudio = {
  /**
   * AudioContext を用意して再生できる状態にする。
   * ブラウザの自動再生制限があるので、必ず最初のユーザー操作の中で呼ぶ。何度呼んでもよい。
   */
  unlock(): void;
  /** 吸い付いた瞬間の SE。unlock 前や音が出せない環境では何もしない。 */
  playSnap(): void;
  /** AudioContext を閉じる。 */
  dispose(): void;
};

/** スナップ SE の再生器。AudioContext は最初の unlock まで作らない。 */
export function createSnapAudio(): SnapAudio {
  let context: AudioContext | null = null;
  let master: GainNode | null = null;
  let unavailable = false;

  /** AudioContext を（無ければ作って）返す。作れない環境では null。 */
  const ensureContext = (): AudioContext | null => {
    if (context !== null) return context;
    if (unavailable) return null;
    const Constructor: typeof AudioContext | undefined = window.AudioContext;
    if (Constructor === undefined) {
      unavailable = true;
      return null;
    }
    context = new Constructor();
    master = context.createGain();
    master.gain.value = 1;
    master.connect(context.destination);
    return context;
  };

  return {
    unlock(): void {
      const ctx = ensureContext();
      if (ctx === null || ctx.state === 'running' || ctx.state === 'closed') return;
      // 拒否されても操作は続けられるので握りつぶす
      void ctx.resume().catch((): void => undefined);
    },

    playSnap(): void {
      const ctx = ensureContext();
      if (ctx === null || master === null || ctx.state === 'closed') return;
      // ユーザー操作から呼ばれる想定だが、まだ suspended なら起こしておく
      if (ctx.state === 'suspended') void ctx.resume().catch((): void => undefined);

      const output = master;
      const now = ctx.currentTime;
      PARTIAL_RATIOS.forEach((ratio, index): void => {
        const oscillator = ctx.createOscillator();
        oscillator.type = index === 0 ? 'triangle' : 'sine';
        const frequency = BASE_FREQUENCY * ratio;
        oscillator.frequency.setValueAtTime(frequency, now);
        // わずかにピッチを落として、余韻が澄んで聞こえるようにする
        oscillator.frequency.exponentialRampToValueAtTime(frequency * 0.94, now + DURATION);

        const gain = ctx.createGain();
        gain.gain.setValueAtTime(SILENCE, now);
        gain.gain.exponentialRampToValueAtTime(PEAK_GAIN / (index + 1), now + ATTACK);
        gain.gain.exponentialRampToValueAtTime(SILENCE, now + DURATION);

        oscillator.connect(gain);
        gain.connect(output);
        oscillator.start(now);
        oscillator.stop(now + DURATION);
        oscillator.onended = (): void => {
          oscillator.disconnect();
          gain.disconnect();
        };
      });
    },

    dispose(): void {
      const ctx = context;
      context = null;
      master = null;
      if (ctx === null || ctx.state === 'closed') return;
      void ctx.close().catch((): void => undefined);
    },
  };
}
