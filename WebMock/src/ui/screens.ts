// 画面遷移（SPEC.md 2 章 / 6 章）。title（難易度選択）→ play → clear の 3 状態だけを持つ。
// DOM とイベントリスナが積み上がらないよう、切り替えのたびに前の画面を必ず dispose する。

/** 1 画面。生成と破棄を対にする。 */
export type Screen = {
  readonly element: HTMLElement;
  /** DOM を取り除く。要素ごと捨てるので、要素に付けたリスナの解除は要らない。 */
  dispose(): void;
};

/** 画面の名前。SPEC.md 2 章のコアゲームループに対応する。 */
export type ScreenName = 'title' | 'play' | 'clear';

export type ScreenManager = {
  /** いま出ている画面。まだ何も出していなければ null。 */
  current(): ScreenName | null;
  /**
   * 画面を差し替える。前の画面は build を呼ぶ前に dispose する。
   * build には UI のルート（index.html の #ui）を渡す。
   */
  show<T extends Screen>(name: ScreenName, build: (container: HTMLElement) => T): T;
  /** いま出ている画面を片付ける。 */
  dispose(): void;
};

/** container（既定は #ui）にひとつだけ画面を出す小さな管理役。 */
export function createScreenManager(container: HTMLElement): ScreenManager {
  let active: Screen | null = null;
  let activeName: ScreenName | null = null;

  const clear = (): void => {
    if (active === null) return;
    active.dispose();
    active = null;
    activeName = null;
  };

  return {
    current(): ScreenName | null {
      return activeName;
    },
    show<T extends Screen>(name: ScreenName, build: (container: HTMLElement) => T): T {
      clear();
      const screen = build(container);
      active = screen;
      activeName = name;
      return screen;
    },
    dispose(): void {
      clear();
    },
  };
}
