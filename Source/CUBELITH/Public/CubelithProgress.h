// 残りピース数（RULES.md 6 章の「未確定のピース数」）の数え上げ。移植元は WebMock/src/ui/progress.ts
// 移植元が src/core ではなく src/ui にあるので、UE 版でも CUBELITHCore ではなく CUBELITH モジュールへ置く
// （CubelithSeed.h / CubelithDifficulty.h と同じ立ち位置）。Three.js にも UObject にも依存しない純粋関数なので
// テストから直接呼べる

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Piece.h"

namespace Cubelith
{
	/**
	 * まだ確定していないピースの数を返す（0 なら全ピースが本体に収まっている。TS の unsettledPieceCount）。
	 * Pieces に無い id の配置は無視する。
	 *
	 * 解釈: RULES.md は「未確定」の定義を書いておらず、ピースに確定という状態も無い。ここでは
	 * 「解答が成立する立方体の一部として整合しているピース」を確定とみなし、次の手順で数える。
	 *   1. 面で接しているピース同士を連結成分（塊）にまとめる
	 *   2. 最大の塊を「組み上がりつつある本体」とする。ただしピース 1 個だけの塊は本体としない
	 *      （散らした直後に 1 個だけ確定扱いになるのを避けるため）
	 *   3. 本体の外接ボックスが N×N×N に収まっていれば、本体のピースを確定とみなす。
	 *      収まっていなければ立方体になり得ないので、その塊は確定ではない
	 * クリアした瞬間は全ピースが 1 つの塊になり外接ボックスが N×N×N なので残りは 0 になる。
	 */
	CUBELITH_API int32 UnsettledPieceCount(
		TArrayView<const FPiece> Pieces, TArrayView<const FPlacement> Placements, int32 N);
}
