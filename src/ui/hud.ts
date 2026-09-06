// プレイ中の HUD（SPEC.md 6 章）。素の HTML + CSS で作り、Three.js のキャンバスに重ねる。
// 上から「ステータス行 / ピース操作行（選択中だけ表示）/ フッター行（常時表示）」の 3 段構成。
// スマホ向けに画面下部へ寄せ、ボタンは 44 px 以上（サイズの保証は index.html の .ui-button）。
// クリア表示はクリア画面（clearScreen.ts）が担うので、ここでは持たない。

import type { Axis } from '../core/grid';
import type { Screen } from './screens';
import { createButton, setSelected as setSelectedStyle } from './widgets';

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
  /** 選択中のピースの固定 / 固定解除を切り替える。 */
  readonly onToggleLock: () => void;
  /** ヒント（未固定のピース 1 つを正解位置へ送って金ロックで固定する）。 */
  readonly onHint: () => void;
};

/**
 * 選択中のピースの固定状態。
 * 'none' = 固定していない、'manual' = 人が固定した（解除できる）、'hint' = ヒントで固定した（解除できない）。
 */
export type LockState = 'none' | 'manual' | 'hint';

export type Hud = Screen & {
  /** 選択中のピースを伝える。null ならピース操作行を隠す。 */
  setSelected(pieceId: number | null): void;
  /** 残りピース数（SPEC.md 6 章）。数え方は progress.ts の「解釈:」を参照。 */
  setRemaining(remaining: number, total: number): void;
  /**
   * 選択中のピースの固定状態を伝える。ボタンのラベルと、動かす操作の有効 / 無効が切り替わる。
   * setSelected で選択を変えたあとに呼ぶ。
   */
  setLock(kind: LockState): void;
  /**
   * ヒントボタンの有効 / 無効。未固定のピースが 1 個以下のときは false にする
   * （最後の 1 ピースはヒントを使えない）。
   */
  setHintEnabled(enabled: boolean): void;
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

  // ピースに紐づく操作（回転 6 個 + 奥行き 2 個 + 固定 1 個）。選択中のピースが無いときは行ごと隠す。
  // 後続タスクで「回転 / 回転解除」がこの中に増える
  const pieceControls = document.createElement('div');
  pieceControls.id = 'hud-piece-controls';

  // 固定中は「動かす操作」を全部無効にする。game が弾くので実害は無いが、押しても無反応な
  // ボタンを見せないために disabled にする
  const movementButtons: HTMLButtonElement[] = [];

  const rotateRow = document.createElement('div');
  rotateRow.className = 'hud-row';
  for (const spec of ROTATE_BUTTONS) {
    const button = createButton(spec.label, 'ui-button', (): void => {
      callbacks.onRotate(spec.axis, spec.dir);
    });
    movementButtons.push(button);
    rotateRow.appendChild(button);
  }
  pieceControls.appendChild(rotateRow);

  const moveRow = document.createElement('div');
  moveRow.className = 'hud-row';
  const depthFar = createButton('奥へ', 'ui-button', (): void => {
    callbacks.onDepth(1);
  });
  const depthNear = createButton('手前へ', 'ui-button', (): void => {
    callbacks.onDepth(-1);
  });
  movementButtons.push(depthFar, depthNear);
  // 固定トグル。ラベルを状態で入れ替える（'none' → 固定 / それ以外 → 固定解除）
  const lockButton = createButton('固定', 'ui-button', callbacks.onToggleLock);
  lockButton.id = 'hud-lock';
  moveRow.append(depthFar, depthNear, lockButton);
  pieceControls.appendChild(moveRow);
  root.appendChild(pieceControls);

  // 常時表示のフッター。左から「難易度選択へ戻る」「散らし直す」「ヒント」「サウンド」「次の問題」
  const footer = document.createElement('div');
  footer.id = 'hud-footer';
  footer.className = 'hud-row';

  // ヒント。使えるかどうかは盤面しだいなので、既定は無効にしておき setHintEnabled で切り替える
  const hintButton = createButton('ヒント', 'ui-button', callbacks.onHint);
  hintButton.id = 'hud-hint-button';

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

  let selectedId: number | null = null;
  let lockState: LockState = 'none';

  /** 選択と固定の状態をボタンと案内文へ反映する。 */
  const render = (): void => {
    const none = selectedId === null;
    pieceControls.style.display = none ? 'none' : '';

    const locked = lockState !== 'none';
    for (const button of movementButtons) button.disabled = locked;
    lockButton.textContent = locked ? '固定解除' : '固定';
    // ヒントの金ロックは解除できない
    lockButton.disabled = lockState === 'hint';
    lockButton.title = lockState === 'hint' ? 'ヒントで置いたピースは解除できない' : '';
    setSelectedStyle(lockButton, lockState === 'manual');

    hint.textContent = none
      ? 'ピースをクリック / タップして選択すると操作ボタンが出る'
      : locked
        ? `選択中: ピース #${selectedId}（固定中 — 動かすには「固定解除」を押す）`
        : `選択中: ピース #${selectedId}（ドラッグで移動 / 2 本指スワイプ・ひねりで回転 / ピンチでズーム）`;
  };

  const setSelected = (pieceId: number | null): void => {
    selectedId = pieceId;
    // 選択が外れたら固定状態も一旦戻す（呼び出し側が続けて setLock で上書きする）
    if (pieceId === null) lockState = 'none';
    render();
  };

  const setLock = (kind: LockState): void => {
    lockState = kind;
    render();
  };
  render();

  const setRemaining = (count: number, total: number): void => {
    remaining.textContent = `残り ${count} / ${total}`;
  };
  setRemaining(0, 0);

  const setHintEnabled = (enabled: boolean): void => {
    hintButton.disabled = !enabled;
    hintButton.title = enabled ? '' : '残り 1 ピースではヒントを使えない';
  };
  setHintEnabled(false);

  return {
    element: root,
    setSelected,
    setRemaining,
    setLock,
    setHintEnabled,
    dispose(): void {
      root.remove();
    },
  };
}
