// 2 本指ジェスチャの認識（SPEC.md 3.3 / 6 章）。Three.js にも DOM にも依存しない純粋な状態機械で、
// 呼び出し側（pieceInput）が 2 本の指の画面座標を渡すと「ピンチ」か「90 度回転」かを返す。
//
// 割り当て（解釈）: SPEC.md 3.3 は回転も奥行き移動も「2 本指スワイプ **または** UI ボタン」を
// 認めている。両方を 2 本指に載せると区別できないので、**2 本指は回転とピンチズームに割り当て、
// 奥行き方向の専用操作は持たない**（カメラを回してその軸を画面上に出してからドラッグで動かす）。
//
// 回転は「閾値を超えたら 1 回だけ」。一度出したら指を離すまで次を出さない（reset で解除）。
// こうしないとスワイプし続ける間ピースが回り続けてしまう。

/** 画面座標の 1 点。 */
export type Point = { readonly x: number; readonly y: number };

/** 認識した操作。 */
export type TwoFingerAction =
  | { readonly kind: 'none' }
  /** ピンチ。scale はカメラ半径に掛ける倍率（< 1 で寄る）。 */
  | { readonly kind: 'zoom'; readonly scale: number }
  /**
   * 90 度回転を 1 回。
   * - `yaw`: 画面の上方向を軸に回す。dir = +1 が「2 本指を右へ」
   * - `pitch`: 画面の右方向を軸に回す。dir = +1 が「2 本指を上へ」
   * - `roll`: 画面の奥方向を軸に回す。dir = +1 が「画面上で時計回りにひねる」
   */
  | { readonly kind: 'rotate'; readonly gesture: 'yaw' | 'pitch' | 'roll'; readonly dir: 1 | -1 };

const NONE: TwoFingerAction = { kind: 'none' };

/** ピンチと判定する指間距離の変化率（開始時の距離に対する比）。 */
export const PINCH_RATIO_THRESHOLD = 0.14;
/** ひねり（Roll）と判定する 2 本指を結ぶ線の回転角（ラジアン。約 26 度）。 */
export const TWIST_RADIANS_THRESHOLD = 0.45;
/** 平行スワイプ（Yaw / Pitch）と判定する 2 本指の中点の移動量（px）。 */
export const SWIPE_PIXELS_THRESHOLD = 44;

export type TwoFingerGesture = {
  /** 2 本目の指が触れた / 指の組が変わったときに基準を取り直す。 */
  reset(a: Point, b: Point): void;
  /** 指が動くたびに呼ぶ。認識した操作を返す（何も無ければ kind: 'none'）。 */
  update(a: Point, b: Point): TwoFingerAction;
};

/** ラジアンを (-PI, PI] に畳む。ひねりの巻き戻りを消す。 */
function normalizeAngle(radians: number): number {
  const wrapped = ((radians + Math.PI) % (Math.PI * 2) + Math.PI * 2) % (Math.PI * 2);
  return wrapped - Math.PI;
}

/**
 * 2 本指の状態機械を作る。
 *
 * 最初に閾値を超えた種類でモードを固定する（ピンチ中に回らない / 回した直後に寄らない）。
 * 三者は「閾値に対する進み具合」で比べるので、単位の違う量でも公平に競わせられる。
 */
export function createTwoFingerGesture(): TwoFingerGesture {
  // 'idle' = まだ種類が決まっていない、'zoom' = ピンチ継続、'done' = 回転を出し終えて打ち止め
  let mode: 'idle' | 'zoom' | 'done' = 'idle';
  let startDistance = 0;
  let startAngle = 0;
  let startCenterX = 0;
  let startCenterY = 0;
  /** ピンチの倍率は「前フレームからの変化」で出すので直近の距離を持つ。 */
  let lastDistance = 0;

  const anchor = (a: Point, b: Point): void => {
    startDistance = Math.hypot(b.x - a.x, b.y - a.y);
    lastDistance = startDistance;
    startAngle = Math.atan2(b.y - a.y, b.x - a.x);
    startCenterX = (a.x + b.x) / 2;
    startCenterY = (a.y + b.y) / 2;
  };

  return {
    reset(a, b): void {
      mode = 'idle';
      anchor(a, b);
    },

    update(a, b): TwoFingerAction {
      if (mode === 'done') return NONE;

      const distance = Math.hypot(b.x - a.x, b.y - a.y);
      if (mode === 'zoom') {
        if (distance <= 0 || lastDistance <= 0) return NONE;
        const scale = lastDistance / distance;
        lastDistance = distance;
        return { kind: 'zoom', scale };
      }

      if (startDistance <= 0 || distance <= 0) return NONE;
      // 3 つの候補を「閾値に対する進み具合」に正規化して比べる
      const pinch = Math.abs(distance / startDistance - 1) / PINCH_RATIO_THRESHOLD;
      const twistAngle = normalizeAngle(Math.atan2(b.y - a.y, b.x - a.x) - startAngle);
      const twist = Math.abs(twistAngle) / TWIST_RADIANS_THRESHOLD;
      const dx = (a.x + b.x) / 2 - startCenterX;
      const dy = (a.y + b.y) / 2 - startCenterY;
      const swipe = Math.hypot(dx, dy) / SWIPE_PIXELS_THRESHOLD;

      if (pinch < 1 && twist < 1 && swipe < 1) return NONE;

      if (pinch >= twist && pinch >= swipe) {
        mode = 'zoom';
        const scale = lastDistance / distance;
        lastDistance = distance;
        return { kind: 'zoom', scale };
      }

      mode = 'done';
      if (twist >= swipe) {
        // 画面座標は Y が下向きなので、角度が増える向き = 画面上の時計回り
        return { kind: 'rotate', gesture: 'roll', dir: twistAngle > 0 ? 1 : -1 };
      }
      if (Math.abs(dx) >= Math.abs(dy)) {
        return { kind: 'rotate', gesture: 'yaw', dir: dx > 0 ? 1 : -1 };
      }
      // 上へスワイプ（clientY が減る）が +1
      return { kind: 'rotate', gesture: 'pitch', dir: dy < 0 ? 1 : -1 };
    },
  };
}
