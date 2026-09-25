// タイトル / 難易度選択画面（RULES.md 6 章）。N と M のプリセットを選んで「開始」。
// シードは小さく表示するだけで、ここでは変えない（タイトルへ来るたびに引き直す）。

import { nearestPreset, piecePresets, spaceSizes } from './difficulty';
import type { Screen } from './screens';
import { createButton, createOptionGroup, setSelected } from './widgets';

/** 「開始」で確定する条件。難易度（N / M / パズルの回転）とシードをまとめて渡す。 */
export type TitleSelection = {
  readonly n: number;
  readonly m: number;
  readonly seed: number;
  /** パズルの回転「あり」なら true。false なら向きは恒等のまま（回転操作の UI も出ない）。 */
  readonly allowRotation: boolean;
};

/** 「続きから」に出す途中の盤面（RULES.md 3.8）。難易度と残りピース数だけで、盤面は復元しない。 */
export type TitleResume = {
  readonly n: number;
  readonly m: number;
  readonly allowRotation: boolean;
  /** 残りピース数（未確定のピース数。RULES.md 6 章）。 */
  readonly remaining: number;
};

export type TitleScreenOptions = {
  /** 初期選択の N。 */
  readonly n: number;
  /** 初期選択の M。プリセットに無い値なら最も近いものへ寄せる。 */
  readonly m: number;
  /** 表示するシード。「開始」でそのまま渡す。 */
  readonly seed: number;
  /** 初期選択の「パズルの回転」（既定は difficulty.ts の DEFAULT_ALLOW_ROTATION = なし）。 */
  readonly allowRotation: boolean;
  /** 途中の盤面（RULES.md 3.8）。無ければ null で、そのときは「続きから」を出さない。 */
  readonly resume: TitleResume | null;
  /** 「開始」が押されたとき。 */
  readonly onStart: (selection: TitleSelection) => void;
  /** 「続きから」が押されたとき。resume が null なら呼ばれない。 */
  readonly onResume: () => void;
  /** これまでのクリア回数の合計（RULES.md 3.8）。 */
  readonly clearTotal: number;
  /** 選択中の難易度のクリア回数。選択を変えるたびに引き直す。 */
  readonly clearCountOf: (n: number, m: number, allowRotation: boolean) => number;
};

/** 「パズルの回転」の表示。まとめの行と「続きから」の説明で同じ文言を使う。 */
function rotationLabel(allowRotation: boolean): string {
  return allowRotation ? '回転あり' : '回転なし';
}

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
  heading.textContent = 'CUBELITH';
  panel.appendChild(heading);

  const lead = document.createElement('p');
  lead.className = 'screen-lead';
  lead.textContent = '空間サイズ・分割数・パズルの回転を選んで開始する';
  panel.appendChild(lead);

  let spaceSize = options.n;
  let pieceCount = options.m;
  let allowRotation = options.allowRotation;

  const sizeGroup = createOptionGroup('空間サイズ N');
  const pieceGroup = createOptionGroup('分割数 M');
  const rotationGroup = createOptionGroup('パズルの回転');
  panel.append(sizeGroup.element, pieceGroup.element, rotationGroup.element);

  const summary = document.createElement('div');
  summary.className = 'screen-lead';
  panel.appendChild(summary);

  // 解釈: 要望で文言と見せ方は実装側の判断でよいとされた点。「続きから」は「開始」の上に
  // 主ボタンとして置き、下に難易度と残りピース数を 1 行添える。途中の盤面が無ければ行ごと出さない
  const resume = options.resume;
  if (resume !== null) {
    panel.appendChild(
      createButton('続きから', 'ui-button ui-button--primary ui-button--wide', options.onResume),
    );
    const resumeNote = document.createElement('div');
    resumeNote.className = 'screen-lead';
    resumeNote.textContent = `N = ${resume.n} / M = ${resume.m} / ${rotationLabel(resume.allowRotation)}・残り ${resume.remaining} ピース`;
    panel.appendChild(resumeNote);
  }

  // 解釈: 「続きから」があるときは主ボタンをそちらへ譲る（同じ強調のボタンを 2 つ並べない）
  const startClass =
    resume === null
      ? 'ui-button ui-button--primary ui-button--wide'
      : 'ui-button ui-button--wide';
  const startButton = createButton('開始', startClass, (): void => {
    options.onStart({ n: spaceSize, m: pieceCount, seed: options.seed, allowRotation });
  });
  panel.appendChild(startButton);

  // クリア回数（RULES.md 3.8）。合計と、いま選んでいる難易度の回数を 1 行で出す
  const clearLine = document.createElement('div');
  clearLine.className = 'screen-lead';
  panel.appendChild(clearLine);

  const seedLine = document.createElement('div');
  seedLine.className = 'screen-seed';
  seedLine.textContent = `seed ${options.seed}`;
  panel.appendChild(seedLine);

  // 選択状態は class の付け替えだけで反映する（DOM とリスナを積み上げない）
  const sizeButtons = new Map<number, HTMLButtonElement>();
  const pieceButtons = new Map<number, HTMLButtonElement>();
  const rotationButtons = new Map<boolean, HTMLButtonElement>();

  const syncSelection = (): void => {
    for (const [value, button] of sizeButtons) setSelected(button, value === spaceSize);
    for (const [value, button] of pieceButtons) setSelected(button, value === pieceCount);
    for (const [value, button] of rotationButtons) setSelected(button, value === allowRotation);
    summary.textContent = `N = ${spaceSize} / M = ${pieceCount} / ${rotationLabel(allowRotation)}（${spaceSize ** 3} ボクセルを ${pieceCount} 個に分割）`;
    const count = options.clearCountOf(spaceSize, pieceCount, allowRotation);
    clearLine.textContent = `クリア 合計 ${options.clearTotal} 回 / この難易度 ${count} 回`;
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

  for (const value of [false, true]) {
    const button = createButton(value ? 'あり' : 'なし', 'ui-button ui-button--option', (): void => {
      allowRotation = value;
      syncSelection();
    });
    rotationButtons.set(value, button);
    rotationGroup.row.appendChild(button);
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
      rotationButtons.clear();
    },
  };
}
