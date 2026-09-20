// クリア画面（SPEC.md 2 章 5 / 6 章）。「もう一度（同じ条件で再生成・シードは変える）」と
// 「難易度を変える（タイトルへ）」の 2 つだけ。
// クリア演出（発光・融合・パーティクル・カメラ旋回）は src/render/clearEffect.ts が持つ。
// この画面は演出の完了を待たずに出し、「もう一度」「難易度を変える」で演出ごと解除される。

import type { Screen } from './screens';
import { createButton } from './widgets';

export type ClearScreenOptions = {
  readonly n: number;
  readonly m: number;
  readonly seed: number;
  /** 同じ N / M でシードだけ変えて作り直す。 */
  readonly onRetry: () => void;
  /** タイトル（難易度選択）へ戻る。 */
  readonly onBackToTitle: () => void;
};

/** container（既定は #ui）にクリア画面を作る。 */
export function createClearScreen(
  container: HTMLElement,
  options: ClearScreenOptions,
): Screen {
  const root = document.createElement('div');
  root.id = 'clear-screen';
  // 完成した立方体が見えるよう、パネルは画面下部に寄せる
  root.className = 'screen screen--bottom';

  const banner = document.createElement('div');
  banner.id = 'clear-banner';
  banner.textContent = 'CLEAR';
  root.appendChild(banner);

  const panel = document.createElement('div');
  panel.className = 'screen-panel';
  root.appendChild(panel);

  const summary = document.createElement('div');
  summary.className = 'screen-lead';
  summary.textContent = `N = ${options.n} / M = ${options.m} を組み上げた`;
  panel.appendChild(summary);

  const row = document.createElement('div');
  row.className = 'option-row';
  row.append(
    createButton('もう一度', 'ui-button ui-button--primary ui-button--wide', options.onRetry),
    createButton('難易度を変える', 'ui-button ui-button--wide', options.onBackToTitle),
  );
  panel.appendChild(row);

  const seedLine = document.createElement('div');
  seedLine.className = 'screen-seed';
  seedLine.textContent = `seed ${options.seed}`;
  panel.appendChild(seedLine);

  container.appendChild(root);

  return {
    element: root,
    dispose(): void {
      root.remove();
    },
  };
}
