// 近接ピック用のサンプル点（ポインタ座標からのずらし量）を作る。
// 移植元は WebMock/src/input/pickSamples.ts。ライントレースそのものは呼び出し側が持つが、
// 「どこへずらしたレイを撃つか」の計算はエンジンの機能に依存しないのでここへ切り出してテストする
// （CubelithAxisMapping.h と同じ扱い）。

#pragma once

#include "CoreMinimal.h"

namespace Cubelith
{
	/** ポインタ座標からのずらし量（画面のピクセル。Dy は画面下向きが +） */
	struct CUBELITH_API FPickOffset
	{
		double Dx = 0.0;
		double Dy = 0.0;
	};

	/**
	 * 既定のリング 1 本あたりの点数とリング本数。
	 * 解釈（移植元のコメントのまま）: 1 回のタップで撃つレイは 1 + 6 * 2 = 13 本。ボクセルの隙間
	 * （0.04 マスの溝）や細いピースの縁を拾うにはこの程度で足り、これ以上増やしても手触りは変わらず負荷だけ増える。
	 */
	inline constexpr double DefaultPickRingPoints = 6.0;
	inline constexpr double DefaultPickRingCount = 2.0;

	/**
	 * 中心 + 同心円状のサンプル点を返す（TS の pickSampleOffsets）。先頭は必ず中心 (0, 0)。
	 *
	 * リングは内側から順に並び、i 本目（1 始まり）の半径は `Radius * i / Rings`。
	 * 隣り合うリングで点が同じ方角に重ならないよう、リングごとに半ステップずつ回してある。
	 *
	 * Radius が有限の正の数でない、または Points / Rings が 1 未満のときは中心 1 点だけを返す
	 * （近接ピックを切ったのと同じ挙動になる）。Points / Rings は TS と同じく小数を受けて切り捨てる。
	 */
	CUBELITH_API TArray<FPickOffset> PickSampleOffsets(
		double Radius, double Points = DefaultPickRingPoints, double Rings = DefaultPickRingCount);
}
