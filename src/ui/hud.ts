// プレイ中の HUD（SPEC.md 6 章）。素の HTML + CSS で作り、Three.js のキャンバスに重ねる。
// 上から「ステータス行 / ピース操作行（選択中だけ表示）/ フッター行（常時表示）」の 3 段構成。
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
  /** 同じ N / M のまま新しいシードで次の問題へ。 */
  readonly onNext: () => void;
};

export type Hud = Screen & {
  /** 選択中のピースを伝える。null ならピース操作行を隠す。 */
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

  // ピースに紐づく操作（回転 6 個 + 奥行き 2 個）。選択中のピースが無いときは行ごと隠す。
  // 後続タスクで「固定 / 固定解除」「回転 / 回転解除」がこの中に増える
  const pieceControls = document.createElement('div');
  pieceControls.id = 'hud-piece-controls';

  const rotateRow = document.createElement('div');
  rotateRow.className = 'hud-row';
  for (const spec of ROTATE_BUTTONS) {
    rotateRow.appendChild(
      createButton(spec.label, 'ui-button', (): void => {
        callbacks.onRotate(spec.axis, spec.dir);
      }),
    );
  }
  pieceControls.appendChild(rotateRow);

  const moveRow = document.createElement('div');
  moveRow.className = 'hud-row';
  moveRow.append(
    createButton('奥へ', 'ui-button', (): void => {
      callbacks.onDepth(1);
    }),
    createButton('手前へ', 'ui-button', (): void => {
      callbacks.onDepth(-1);
    }),
  );
  pieceControls.appendChild(moveRow);
  root.appendChild(pieceControls);

  // 常時表示のフッター。左から「難易度選択へ戻る」「散らし直す」「ヒント」「サウンド」「次の問題」
  const footer = document.createElement('div');
  footer.id = 'hud-footer';
  footer.className = 'hud-row';

  // ヒント本体は後続タスクで実装する。そこでこのボタンの disabled を外す
  const hintButton = createButton('ヒント', 'ui-button', (): void => {});
  hintButton.disabled = true;
  hintButton.title = '未実装';

  // サウンドの on / off は未実装。ラベルだけ置いておく
  const soundButton = createButton('サウンド', 'ui-button', (): void => {});
  soundButton.disabled = true;
  soundButton.title = '未実装';

  footer.append(
    createButton('難易度選択へ戻る', 'ui-button', callbacks.onBackToTitle),
    createButton('散らし直す', 'ui-button', callbacks.onReset),
    hintButton,
    soundButton,
    createButton('次の問題', 'ui-button', callbacks.onNext),
  );
  root.appendChild(footer);

  container.appendChild(root);

  const setSelected = (pieceId: number | null): void => {
    const none = pieceId === null;
    pieceControls.style.display = none ? 'none' : '';
    hint.textContent = none
      ? 'ピースをクリック / タップして選択すると操作ボタンが出る'
      : `選択中: ピース #${pieceId}（ドラッグで移動 / 2 本指スワイプ・ひねりで回転 / ピンチでズーム）`;
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
