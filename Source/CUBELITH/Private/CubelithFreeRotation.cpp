// 自由回転 → 最寄りの向き id（移植元 WebMock/src/input/freeRotation.ts）
//
// ■ 何を変換しているか
// TS 版は three.js のクォータニオンもロジックも同じ座標系（Y が上の右手系）なので、
// 列優先 → 行優先の並べ替えだけで済んでいた。UE 版は座標系そのものが違う（Z が上・左手系）ので、
// Docs/SPEC_UE.md 7.2 の置換 P による写し替え R_logic = P · R_UE · P が 1 段増える。
// P は y と z を入れ替える置換で P · P = 恒等（自分自身が逆行列）。det(P) = -1 だが
// R_logic = P · R_UE · P では det が打ち消えるので鏡像にはならない（CubelithCoords.cpp の説明と同じ）。
//
// ■ 行列の規約
// - FMatrix（UE）は行ベクトルを左から掛ける（v' = v · M）
// - FMat3 / NearestOrientation（CUBELITHCore）は列ベクトルに左から掛ける（v' = M · v）の行優先
// 転置は WorldRotationToLogicRowMajor の中だけで行い、それ以降は列ベクトル規約の行優先で通す。

#include "CubelithFreeRotation.h"

#include "Grid.h"
#include "Math/QuatRotationTranslationMatrix.h"

namespace Cubelith
{
	namespace
	{
		/**
		 * 置換 P に対応する添字の読み替え（0 → 0, 1 → 2, 2 → 1）。
		 * P は置換行列なので、P · R · P は「行と列の添字をこの表で読み替えるだけ」で作れる:
		 *   (P · R · P)(i, j) = R(SwapAxis[i], SwapAxis[j])
		 * CubelithCoords.cpp の同名の表と同じもの（向きが逆の変換で同じ P を使う）。
		 */
		constexpr int32 SwapAxis[3] = { 0, 2, 1 };

		/** 行優先・列ベクトル規約の 3x3 の積 C = A · B */
		void Multiply3x3(const double* A, const double* B, double* Out)
		{
			for (int32 Row = 0; Row < 3; ++Row)
			{
				for (int32 Col = 0; Col < 3; ++Col)
				{
					double Sum = 0.0;
					for (int32 K = 0; K < 3; ++K)
					{
						Sum += A[Row * 3 + K] * B[K * 3 + Col];
					}
					Out[Row * 3 + Col] = Sum;
				}
			}
		}
	}

	TArray<double> WorldRotationToLogicRowMajor(const FMatrix& WorldMatrix)
	{
		TArray<double> Result;
		Result.SetNumUninitialized(9);

		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Col = 0; Col < 3; ++Col)
			{
				// R_logic(Row, Col) = R_UE(SwapAxis[Row], SwapAxis[Col])、
				// かつ R_UE(i, j) = WorldMatrix.M[j][i]（FMatrix は転置して持っている）
				Result[Row * 3 + Col] = WorldMatrix.M[SwapAxis[Col]][SwapAxis[Row]];
			}
		}

		return Result;
	}

	int32 SnappedOrientation(int32 Orientation, const FQuat& WorldQuat)
	{
		checkf(Orientation >= 0 && Orientation < OrientationCount,
			TEXT("向き id は 0..%d (got %d)"), OrientationCount - 1, Orientation);

		// 自由回転をロジック座標へ（行優先・列ベクトル規約）
		const TArray<double> Free = WorldRotationToLogicRowMajor(FQuatRotationMatrix(WorldQuat));

		// 今の向き（ロジックの整数回転行列）。これも行優先・列ベクトル規約
		const FMat3 Logic = OrientationMatrix(Orientation);
		double LogicValues[9];
		for (int32 Index = 0; Index < 9; ++Index)
		{
			LogicValues[Index] = static_cast<double>(Logic.M[Index]);
		}

		// 「向き Logic で置いたピースを、あとから Free で回した」姿勢 = Free · Logic（左から掛ける）
		double Composed[9];
		Multiply3x3(Free.GetData(), LogicValues, Composed);

		return NearestOrientation(TArrayView<const double>(Composed, 9));
	}
}
