// 計測用の簡易 FPS 表示（`?fps=1` で出る）。仕様書には無い開発用の道具なので、
// 指定が無いときは DOM ごと作らない。
//
// 表示は「fps / ドローコール数」。ドローコールは SPEC.md 4 章の
// 「1 ピース = 1 InstancedMesh」がそのまま効いているか（＝ピース数 + α に収まっているか）を
// 目で確かめるために出す。

import type { Screen } from './screens';

/** 表示を書き換える間隔（秒）。毎フレーム書くと数字が読めない。 */
const REFRESH_SECONDS = 0.4;

export type FpsMeter = Screen & {
  /** 毎フレーム呼ぶ。delta は秒、drawCalls は直前のフレームのドローコール数。 */
  update(deltaSeconds: number, drawCalls: number): void;
};

/** container（既定は #ui）に FPS 表示を作る。 */
export function createFpsMeter(container: HTMLElement): FpsMeter {
  const root = document.createElement('div');
  root.id = 'fps-meter';
  root.textContent = '-- fps';
  container.appendChild(root);

  let elapsed = 0;
  let frames = 0;

  return {
    element: root,
    update(deltaSeconds: number, drawCalls: number): void {
      elapsed += deltaSeconds;
      frames += 1;
      if (elapsed < REFRESH_SECONDS) return;
      const fps = Math.round(frames / elapsed);
      root.textContent = `${fps} fps / draw ${drawCalls}`;
      elapsed = 0;
      frames = 0;
    },
    dispose(): void {
      root.remove();
    },
  };
}
