// タイトル / 難易度選択画面（SPEC.md 6 章）。N と M のプリセットを選んで「開始」。
// シードは小さく表示するだけで、ここでは変えない（タイトルへ来るたびに引き直す）。

import { nearestPreset, piecePresets, spaceSizes } from './difficulty';
import type { Screen } from './screens';
import { createButton, createOptionGroup, setSelected } from './widgets';

export type TitleScreenOptions = {
  /** 初期選択の N。 */
  readonly n: number;
  /** 初期選択の M。プリセットに無い値なら最も近いものへ寄せる。 */
  readonly m: number;
  /** 表示するシード。「開始」でそのまま渡す。 */
  readonly seed: number;
  /** 「開始」が押されたとき。 */
  readonly onStart: (n: number, m: number, seed: number) => void;
};

/** container（既定は #ui）にタイトル画面を作る。 */
export function createTitleScreen(
  container: HTMLElement,
  options: TitleScreenOptions,
): Screen {
  const root = document.createElement('div');
  root.id = 'title-screen';
  root.className = 'screen screen--center';

  const panel = document.createElement('div');
  panel.className = 'screen-panel';
  root.appendChild(panel);

  const heading = document.createElement('h1');
  heading.className = 'screen-title';
  heading.textContent = 'Crystal Assemble';
  panel.appendChild(heading);

  const lead = document.createElement('p');
  lead.className = 'screen-lead';
  lead.textContent = '空間サイズと分割数を選んで開始する';
  panel.appendChild(lead);

  let spaceSize = options.n;
  let pieceCount = options.m;

  const sizeGroup = createOptionGroup('空間サイズ N');
  const pieceGroup = createOptionGroup('分割数 M');
  panel.append(sizeGroup.element, pieceGroup.element);

  const summary = document.createElement('div');
  summary.className = 'screen-lead';
  panel.appendChild(summary);

  const startButton = createButton('開始', 'ui-button ui-button--primary ui-button--wide', (): void => {
    options.onStart(spaceSize, pieceCount, options.seed);
  });
  panel.appendChild(startButton);

  const seedLine = document.createElement('div');
  seedLine.className = 'screen-seed';
  seedLine.textContent = `seed ${options.seed}`;
  panel.appendChild(seedLine);

  // 選択状態は class の付け替えだけで反映する（DOM とリスナを積み上げない）
  const sizeButtons = new Map<number, HTMLButtonElement>();
  const pieceButtons = new Map<number, HTMLButtonElement>();

  const syncSelection = (): void => {
    for (const [value, button] of sizeButtons) setSelected(button, value === spaceSize);
    for (const [value, button] of pieceButtons) setSelected(button, value === pieceCount);
    summary.textContent = `N = ${spaceSize} / M = ${pieceCount}（${spaceSize ** 3} ボクセルを ${pieceCount} 個に分割）`;
  };

  /** N が変わると M の有効範囲が変わるので、プリセットのボタンごと作り直す。 */
  const rebuildPieceOptions = (): void => {
    const presets = piecePresets(spaceSize);
    pieceCount = nearestPreset(presets, pieceCount);
    pieceButtons.clear();
    pieceGroup.row.replaceChildren();
    for (const value of presets) {
      const button = createButton(String(value), 'ui-button ui-button--option', (): void => {
        pieceCount = value;
        syncSelection();
      });
      pieceButtons.set(value, button);
      pieceGroup.row.appendChild(button);
    }
  };

  for (const value of spaceSizes()) {
    const button = createButton(String(value), 'ui-button ui-button--option', (): void => {
      if (spaceSize === value) return;
      spaceSize = value;
      rebuildPieceOptions();
      syncSelection();
    });
    sizeButtons.set(value, button);
    sizeGroup.row.appendChild(button);
  }

  rebuildPieceOptions();
  syncSelection();

  container.appendChild(root);

  return {
    element: root,
    dispose(): void {
      root.remove();
      sizeButtons.clear();
      pieceButtons.clear();
    },
  };
}
