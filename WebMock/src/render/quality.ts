// 描画品質の既定値を決める粗い端末判定（SPEC.md 4 章の「スマホ 30 fps 以上」）。
//
// すりガラス（MeshPhysicalMaterial の transmission）は three が屈折用に画面をもう一度
// レンダリングするため、モバイル GPU では一気に重くなる。そこで **軽量モード**
// （半透明 MeshStandardMaterial + 稜線の発光）へのフォールバックを用意してある。
//
// 切り替え手段:
//   ?lite=1 … 軽量モードを強制する
//   ?lite=0 … すりガラスを強制する（低スペック判定でも使う）
//   指定なし … この preferLiteMode() の判定に従う

/** navigator の任意プロパティ（型定義に無いものがある）。 */
type CapabilityHints = Navigator & {
  readonly deviceMemory?: number;
  readonly maxTouchPoints?: number;
};

/** 軽量モードの目安にする搭載メモリ（GB）と論理コア数の上限。 */
const LOW_MEMORY_GB = 4;
const LOW_CORE_COUNT = 4;

/**
 * 軽量モードを既定にするか。判定できる材料が無ければ false（＝すりガラス）。
 *
 * 解釈: SPEC.md は端末判定の方法まで決めていない。GPU の性能を直接測る手段は無いので、
 * 「モバイルらしさ（タッチ主体）」と「メモリ / コア数の少なさ」という粗い手掛かりで決める。
 * 外す可能性はあるので、`?lite=0` / `?lite=1` で人が上書きできるようにしてある。
 */
export function preferLiteMode(): boolean {
  if (typeof navigator === 'undefined') return false;
  const hints = navigator as CapabilityHints;

  if (typeof hints.deviceMemory === 'number' && hints.deviceMemory <= LOW_MEMORY_GB) return true;
  if (
    typeof hints.hardwareConcurrency === 'number' &&
    hints.hardwareConcurrency <= LOW_CORE_COUNT
  ) {
    return true;
  }
  // 粗い判定の最後の砦。ポインタが指しか無い端末はモバイルとみなす
  if (typeof window !== 'undefined' && typeof window.matchMedia === 'function') {
    return window.matchMedia('(any-pointer: coarse) and (any-hover: none)').matches;
  }
  return false;
}
