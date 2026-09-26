// 画面のドラッグ方向を、カメラの向きに応じてロジックのグリッド軸へ写像する（Docs/RULES.md 3.3「移動」）。
// 移植元は WebMock/src/input/axisMapping.ts。カメラ依存の計算なので CUBELITHCore には置かず、
// 描画・入力側の CUBELITH モジュールに置く（Docs/SPEC_UE.md 4 章「純粋関数はテストごと C++ へ移す」）。
// ここ自体はカメラの型（UCameraComponent など）に依存しない純粋な関数で、
// 呼び出し側がカメラの基底ベクトルを取り出して渡す。

#pragma once

#include "CoreMinimal.h"

#include "Grid.h"

namespace Cubelith
{
	/** グリッド軸と符号の組。Count マス動かすと Axis 方向へ Sign * Count 進む（TS の AxisStep） */
	struct CUBELITH_API FAxisStep
	{
		/** 移動する軸（ロジック座標の x / y / z） */
		EAxis Axis = EAxis::X;

		/** +1 または -1 */
		int32 Sign = 1;
	};

	inline bool operator==(const FAxisStep& A, const FAxisStep& B)
	{
		return A.Axis == B.Axis && A.Sign == B.Sign;
	}

	inline bool operator!=(const FAxisStep& A, const FAxisStep& B)
	{
		return !(A == B);
	}

	/** ドラッグ 2 軸（画面の右 / 上）と、残る 1 軸（奥行き）の割り当て（TS の DragAxes） */
	struct CUBELITH_API FDragAxes
	{
		/** 画面を右へ動かしたときに進む向き */
		FAxisStep Right;

		/** 画面を上へ動かしたときに進む向き */
		FAxisStep Up;

		/** 奥（カメラから遠ざかる側）へ進む向き */
		FAxisStep Depth;
	};

	/**
	 * カメラの基底ベクトルからドラッグ軸の割り当てを作る（TS の dragAxes）。
	 *
	 * 引数は**ロジック座標**（Y が上の右手系）のベクトル。UE のワールド基底からロジック座標へ直すのは
	 * 呼び出し側の仕事で、ここでは変換しない。TS が整数格子用の Vec3 を比較のために流用しているところは、
	 * 実数のカメラ基底が来るので FVector（double）で受ける。
	 *
	 * Right / Up / Forward は正規化されていなくてよい（比較に使うのは成分の大小だけ）。
	 * Forward はカメラが見ている方向で、Depth はそれに最も近い残りの軸になる。
	 * Right と Up が同じ軸を向く縮退した場合でも 3 軸が重ならないよう、選んだ軸を順に除外していく。
	 */
	CUBELITH_API FDragAxes DragAxes(const FVector& Right, const FVector& Up, const FVector& Forward);

	/** 軸ステップを Count マス分の移動ベクトルにする（TS の axisStepVector）。戻り値はグリッドの移動量なので整数 */
	CUBELITH_API FVec3 AxisStepVector(const FAxisStep& Step, int32 Count);
}
