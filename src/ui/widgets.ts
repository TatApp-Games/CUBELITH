// 3 画面で共有する DOM の小道具（SPEC.md 6 章）。素の HTML + CSS だけで作る。
// ボタンの最小サイズ（指で押せる 44 px 以上）は index.html の .ui-button で保証する。

/** クリックで onClick を呼ぶボタン。要素ごと捨てられるので個別のリスナ解除は要らない。 */
export function createButton(
  label: string,
  className: string,
  onClick: () => void,
): HTMLButtonElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = className;
  button.textContent = label;
  button.addEventListener('click', onClick);
  return button;
}

/** ラベル付きの選択肢の行。row に選択肢のボタンを並べる。 */
export type OptionGroup = {
  readonly element: HTMLElement;
  readonly row: HTMLElement;
};

/** 「空間サイズ N」のような見出しと、その下の選択肢の行を作る。 */
export function createOptionGroup(label: string): OptionGroup {
  const element = document.createElement('div');
  element.className = 'option-group';

  const title = document.createElement('div');
  title.className = 'option-label';
  title.textContent = label;

  const row = document.createElement('div');
  row.className = 'option-row';

  element.append(title, row);
  return { element, row };
}

/** 選択状態を見た目と支援技術の両方へ反映する。 */
export function setSelected(button: HTMLButtonElement, selected: boolean): void {
  button.classList.toggle('is-selected', selected);
  button.setAttribute('aria-pressed', String(selected));
}
