// 回転モードの自由回転（UE のクォータニオン）を、90 度単位の向き id へ写す（Docs/RULES.md 3.3）。
// 移植元は WebMock/src/input/freeRotation.ts。CubelithAxisMapping.h と同じく
// 「画面 / カメラの都合をロジックの言葉へ翻訳する」層で、CUBELITHCore は UE の数学型に依存しても
// 座標系は Web 版と同じ（Y が上の右手系）なので、UE ワールド（Z が上の左手系）との行き来はここで済ませ、
// CUBELITHCore へは数値の並びだけを渡す（Docs/SPEC_UE.md 7.2）。

#pragma once

#include "CoreMinimal.h"

namespace Cubelith
{
	/**
	 * UE ワールドの回転行列を、ロジック座標の行優先 3x3（9 要素）に並べ替える。
	 *
	 * 規約が 2 つ混ざるところなので明記しておく:
	 * - 引数 WorldMatrix は UE の FMatrix で、**行ベクトルを左から掛ける**規約（v' = v · M）
	 * - 戻り値は Cubelith::FMat3 / NearestOrientation と同じ、**列ベクトルに左から掛ける**規約（v' = M · v）の
	 *   行優先 9 要素（Out[Row * 3 + Col]）
	 *
	 * 座標系の写し替えには Docs/SPEC_UE.md 7.2 の置換 P（y と z の入れ替え）を使い、R_logic = P · R_UE · P とする。
	 * Cubelith::OrientationToWorldMatrix が同じ P で逆向きの変換をしているので、この 2 つは互いの逆になる。
	 * 必要な転置はこの関数 1 か所に閉じ込めてあり、呼び出し側は行優先・列ベクトル規約だけを意識すればよい。
	 */
	CUBELITH_API TArray<double> WorldRotationToLogicRowMajor(const FMatrix& WorldMatrix);

	/**
	 * 今の向き Orientation のピースに自由回転 WorldQuat を掛けた姿勢に、最も近い向き id を返す（TS の snappedOrientation）。
	 *
	 * 表示は「向き M で置いたピースを、あとから q で回したもの」なので、合成は左から掛けた q · M になる
	 * （TS の composedMatrix = freeMatrix * logicalMatrix と同じ順）。
	 * WorldQuat は UE のワールド空間（Z が上・左手系）の自由回転で、ロジック座標へ直してから
	 * OrientationMatrix(Orientation) へ左から掛け、その 9 要素を NearestOrientation に渡す。
	 */
	CUBELITH_API int32 SnappedOrientation(int32 Orientation, const FQuat& WorldQuat);
}
