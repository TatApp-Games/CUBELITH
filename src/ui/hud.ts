// プレイ中の最小限の HUD（SPEC.md 6 章）。素の HTML + CSS で作り、キャンバスに重ねる。
// 回転 6 個・奥行き移動 2 個・「散らし直す」と、クリア表示だけ。
// 難易度選択画面と残りピース数は M4、演出は M5 のタスクで足す。

import type { Axis } from '../core/grid';

export type HudCallbacks = {
  /** Pitch / Yaw / Roll の ± 回転。 */
  readonly onRotate: (axis: Axis, dir: 1 | -1) => void;
  /** 奥行き移動（+1 = カメラから遠ざかる）。 */
  readonly onDepth: (dir: 1 | -1) => void;
  /** 同じ seed の散らし配置に戻す。 */
  readonly onReset: () => void;
};

export type Hud = {
  readonly element: HTMLElement;
  /** 選択中のピースを伝える。null なら操作ボタンを無効にする。 */
  setSelected(pieceId: number | null): void;
  /** クリア表示の出し入れ。 */
  setSolved(solved: boolean): void;
  /** DOM を取り除く。 */
  dispose(): void;
};

/** 回転ボタンの並び。ラベルは SPEC.md 6 章の Pitch± / Yaw± / Roll±。 */
const ROTATE_BUTTONS: readonly { readonly label: string; readonly axis: Axis; readonly dir: 1 | -1 }[] = [
  { label: 'Pitch +', axis: 'x', dir: 1 },
  { label: 'Pitch −', axis: 'x', dir: -1 },
  { label: 'Yaw +', axis: 'y', dir: 1 },
  { label: 'Yaw −', axis: 'y', dir: -1 },
  { label: 'Roll +', axis: 'z', dir: 1 },
  { label: 'Roll −', axis: 'z', dir: -1 },
];

/** HUD のボタンを作る。 */
function createButton(label: string, className: string, onClick: () => void): HTMLButtonElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = className;
  button.textContent = label;
  button.addEventListener('click', onClick);
  return button;
}

/** container（既定は #ui）に HUD を作って返す。 */
export function createHud(container: HTMLElement, callbacks: HudCallbacks): Hud {
  const root = document.createElement('div');
  root.id = 'hud';

  const status = document.createElement('div');
  status.id = 'hud-status';
  root.appendChild(status);

  // 選択中だけ押せるボタン群（回転 6 個 + 奥行き 2 個）
  const pieceControls: HTMLButtonElement[] = [];

  const rotateRow = document.createElement('div');
  rotateRow.className = 'hud-row';
  for (const spec of ROTATE_BUTTONS) {
    const button = createButton(spec.label, 'hud-button', (): void => {
      callbacks.onRotate(spec.axis, spec.dir);
    });
    pieceControls.push(button);
    rotateRow.appendChild(button);
  }
  root.appendChild(rotateRow);

  const moveRow = document.createElement('div');
  moveRow.className = 'hud-row';
  const farButton = createButton('奥へ', 'hud-button', (): void => {
    callbacks.onDepth(1);
  });
  const nearButton = createButton('手前へ', 'hud-button', (): void => {
    callbacks.onDepth(-1);
  });
  pieceControls.push(farButton, nearButton);
  moveRow.append(farButton, nearButton);
  moveRow.appendChild(
    createButton('散らし直す', 'hud-button hud-button-wide', (): void => {
      callbacks.onReset();
    }),
  );
  root.appendChild(moveRow);

  const banner = document.createElement('div');
  banner.id = 'hud-clear';
  banner.textContent = 'CLEAR';
  banner.hidden = true;

  container.append(root, banner);

  const setSelected = (pieceId: number | null): void => {
    const none = pieceId === null;
    for (const button of pieceControls) button.disabled = none;
    status.textContent = none
      ? 'ピースをクリック / タップして選択'
      : `選択中: ピース #${pieceId}（ドラッグで移動 / ホイールで奥行き）`;
  };
  setSelected(null);

  return {
    element: root,
    setSelected,
    setSolved(solved: boolean): void {
      banner.hidden = !solved;
    },
    dispose(): void {
      root.remove();
      banner.remove();
    },
  };
}
