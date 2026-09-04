// プレイ中の HUD（SPEC.md 6 章）。素の HTML + CSS で作り、Three.js のキャンバスに重ねる。
// 回転 6 個・奥行き移動 2 個・「散らし直す」・「難易度へ戻る」・残りピース数。
// スマホ向けに画面下部へ寄せ、ボタンは 44 px 以上（サイズの保証は index.html の .ui-button）。
// クリア表示はクリア画面（clearScreen.ts）が担うので、ここでは持たない。

import type { Axis } from '../core/grid';
import type { Screen } from './screens';
import { createButton } from './widgets';

export type HudCallbacks = {
  /** Pitch / Yaw / Roll の ± 回転。 */
  readonly onRotate: (axis: Axis, dir: 1 | -1) => void;
  /** 奥行き移動（+1 = カメラから遠ざかる）。 */
  readonly onDepth: (dir: 1 | -1) => void;
  /** 同じ seed の散らし配置に戻す（SPEC.md 3.3「やり直し」）。 */
  readonly onReset: () => void;
  /** タイトル（難易度選択）へ戻る。 */
  readonly onBackToTitle: () => void;
};

export type Hud = Screen & {
  /** 選択中のピースを伝える。null なら回転 / 奥行きボタンを無効表示にする。 */
  setSelected(pieceId: number | null): void;
  /** 残りピース数（SPEC.md 6 章）。数え方は progress.ts の「解釈:」を参照。 */
  setRemaining(remaining: number, total: number): void;
};

/** 回転ボタンの並び。ラベルは SPEC.md 6 章の Pitch± / Yaw± / Roll±。 */
const ROTATE_BUTTONS: readonly {
  readonly label: string;
  readonly axis: Axis;
  readonly dir: 1 | -1;
}[] = [
  { label: 'Pitch +', axis: 'x', dir: 1 },
  { label: 'Pitch −', axis: 'x', dir: -1 },
  { label: 'Yaw +', axis: 'y', dir: 1 },
  { label: 'Yaw −', axis: 'y', dir: -1 },
  { label: 'Roll +', axis: 'z', dir: 1 },
  { label: 'Roll −', axis: 'z', dir: -1 },
];

/** container（既定は #ui）に HUD を作る。 */
export function createHud(container: HTMLElement, callbacks: HudCallbacks): Hud {
  const root = document.createElement('div');
  root.id = 'hud';

  const status = document.createElement('div');
  status.id = 'hud-status';
  const remaining = document.createElement('span');
  remaining.id = 'hud-remaining';
  const hint = document.createElement('span');
  hint.id = 'hud-hint';
  status.append(remaining, hint);
  root.appendChild(status);

  // アクティブなピースがあるときだけ押せるボタン群（回転 6 個 + 奥行き 2 個）
  const pieceControls: HTMLButtonElement[] = [];

  const rotateRow = document.createElement('div');
  rotateRow.className = 'hud-row';
  for (const spec of ROTATE_BUTTONS) {
    const button = createButton(spec.label, 'ui-button', (): void => {
      callbacks.onRotate(spec.axis, spec.dir);
    });
    pieceControls.push(button);
    rotateRow.appendChild(button);
  }
  root.appendChild(rotateRow);

  const moveRow = document.createElement('div');
  moveRow.className = 'hud-row';
  const farButton = createButton('奥へ', 'ui-button', (): void => {
    callbacks.onDepth(1);
  });
  const nearButton = createButton('手前へ', 'ui-button', (): void => {
    callbacks.onDepth(-1);
  });
  pieceControls.push(farButton, nearButton);
  moveRow.append(farButton, nearButton);
  root.appendChild(moveRow);

  const systemRow = document.createElement('div');
  systemRow.className = 'hud-row';
  systemRow.append(
    createButton('散らし直す', 'ui-button ui-button--wide', callbacks.onReset),
    createButton('難易度へ戻る', 'ui-button ui-button--wide', callbacks.onBackToTitle),
  );
  root.appendChild(systemRow);

  container.appendChild(root);

  const setSelected = (pieceId: number | null): void => {
    const none = pieceId === null;
    for (const button of pieceControls) button.disabled = none;
    hint.textContent = none
      ? 'ピースをクリック / タップして選択'
      : `選択中: ピース #${pieceId}（ドラッグで移動 / ホイールで奥行き）`;
  };
  setSelected(null);

  const setRemaining = (count: number, total: number): void => {
    remaining.textContent = `残り ${count} / ${total}`;
  };
  setRemaining(0, 0);

  return {
    element: root,
    setSelected,
    setRemaining,
    dispose(): void {
      root.remove();
    },
  };
}
