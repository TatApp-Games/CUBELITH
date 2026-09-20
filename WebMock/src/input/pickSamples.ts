// 近接ピック用のサンプル点（ポインタ座標からのずらし量）を作る。
// レイキャストそのものは pieceInput が持つが、「どこへずらしたレイを撃つか」の計算は
// Three.js に依存しないのでここへ切り出してテストする（axisMapping.ts と同じ扱い）。

/** ポインタ座標からのずらし量（CSS ピクセル。dy は画面下向きが +）。 */
export type PickOffset = { readonly dx: number; readonly dy: number };

/** ずらし無し（中心）。 */
const CENTER: PickOffset = { dx: 0, dy: 0 };

/**
 * 既定のリング 1 本あたりの点数とリング本数。
 * 解釈: 1 回の pointerdown で撃つレイは 1 + 6 * 2 = 13 本。ボクセルの隙間（0.04 マスの溝）や
 * 細いピースの縁を拾うにはこの程度で足り、これ以上増やしても手触りは変わらず負荷だけ増える。
 */
const DEFAULT_RING_POINTS = 6;
const DEFAULT_RING_COUNT = 2;

/**
 * 中心 + 同心円状のサンプル点を返す。先頭は必ず中心 (0, 0)。
 *
 * リングは内側から順に並び、i 本目（1 始まり）の半径は `radius * i / rings`。
 * 隣り合うリングで点が同じ方角に重ならないよう、リングごとに半ステップずつ回してある。
 *
 * radius が有限の正の数でない、または points / rings が 1 未満のときは中心 1 点だけを返す
 * （近接ピックを切ったのと同じ挙動になる）。
 */
export function pickSampleOffsets(
  radius: number,
  points: number = DEFAULT_RING_POINTS,
  rings: number = DEFAULT_RING_COUNT,
): readonly PickOffset[] {
  if (!Number.isFinite(radius) || radius <= 0) return [CENTER];
  if (!Number.isFinite(points) || !Number.isFinite(rings)) return [CENTER];
  const pointCount = Math.floor(points);
  const ringCount = Math.floor(rings);
  if (pointCount < 1 || ringCount < 1) return [CENTER];

  const offsets: PickOffset[] = [CENTER];
  const step = (Math.PI * 2) / pointCount;
  for (let ring = 1; ring <= ringCount; ring += 1) {
    const ringRadius = (radius * ring) / ringCount;
    // 内側のリングとの位相差。2 本目以降が 1 本目の点の「間」を埋めるようにする
    const phase = (step * (ring - 1)) / ringCount;
    for (let k = 0; k < pointCount; k += 1) {
      const angle = phase + step * k;
      offsets.push({ dx: ringRadius * Math.cos(angle), dy: ringRadius * Math.sin(angle) });
    }
  }
  return offsets;
}
