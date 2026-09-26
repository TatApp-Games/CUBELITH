// ロジック座標 → UE 座標の変換（Docs/SPEC_UE.md 7.2）
//
// ■ 変換の意味
// ロジック（CUBELITHCore）のボクセル座標は Web 版と同じ「右手系・Y が上・整数」。
// UE のワールドは「左手系・Z が上・cm」。Docs/SPEC_UE.md 7.2 の決まりどおり、
//   UE の (X, Y, Z) = (x, z, y) × VoxelSizeCm
// と、y と z を入れ替えるだけで写す。
//
// ■ 置換 P
// この入れ替えは 3x3 の置換行列
//   P = [[1, 0, 0],
//        [0, 0, 1],
//        [0, 1, 0]]
// による写像そのもの（P·(x, y, z) = (x, z, y)）。P·P = 恒等なので P は自分自身が逆行列。
// 向き（ロジックの整数回転行列 R）を UE の回転に直すときも同じ P を使い、R_UE = P · R · P とする。
// これは「ロジックで回してから写す」と「写してから UE で回す」が一致するということ:
//   P · (R · v) = (P · R · P) · (P · v) = R_UE · (P · v)
// テスト CUBELITH.Render.Coords.RotationMatchesRotateVoxel がこの等式そのものを確かめている。
//
// ■ なぜ鏡像にならないか
// det(P) = -1 なので P 単体は反転（鏡像）だが、R_UE = P · R · P では
//   det(R_UE) = det(P) · det(R) · det(P) = (-1) · (+1) · (-1) = +1
// となり、反転が打ち消える。つまり「座標系の手が変わる」のと「向きを写す」のが同じ P で辻褄が合い、
// 形も向きも鏡像にならない。テスト CUBELITH.Render.Coords.NoMirror が 24 通りすべてで det = +1 を確かめる。
//
// ■ 行列の規約
// FMat3（CUBELITHCore）は行優先で、列ベクトルを右から掛ける（v' = M · v）。
// FMatrix（UE）は行ベクトルを左から掛ける（v' = v · M）。
// そのため R_UE をそのまま詰めるのではなく転置して詰める必要があり、
//   FMatrix.M[Row][Col] = R_UE(Col, Row)
// としている。この向きが正しいことは上記 RotationMatchesRotateVoxel で確認済み。

#include "CubelithCoords.h"

namespace Cubelith
{
	namespace
	{
		/**
		 * 置換 P に対応する添字の読み替え（0 → 0, 1 → 2, 2 → 1）。
		 * P は置換行列なので、P · R · P は「行と列の添字をこの表で読み替えるだけ」で作れる:
		 *   (P · R · P)(i, j) = R(SwapAxis[i], SwapAxis[j])
		 */
		constexpr int32 SwapAxis[3] = { 0, 2, 1 };
	}

	FVector VoxelToWorld(const FVec3& Voxel)
	{
		// UE の (X, Y, Z) = (x, z, y) × VoxelSizeCm
		return FVector(
			static_cast<double>(Voxel.X) * VoxelSizeCm,
			static_cast<double>(Voxel.Z) * VoxelSizeCm,
			static_cast<double>(Voxel.Y) * VoxelSizeCm);
	}

	FMatrix OrientationToWorldMatrix(int32 Orientation)
	{
		checkf(Orientation >= 0 && Orientation < OrientationCount,
			TEXT("向き id は 0..%d (got %d)"), OrientationCount - 1, Orientation);

		const FMat3 R = OrientationMatrix(Orientation);

		// 平行移動 0・スケール 1。4 行目 / 4 列目は恒等のまま使い、左上 3x3 だけ差し替える
		FMatrix Result = FMatrix::Identity;
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Col = 0; Col < 3; ++Col)
			{
				// R_UE(Col, Row) = R(SwapAxis[Col], SwapAxis[Row])。FMatrix は転置して詰める（上のコメント参照）
				Result.M[Row][Col] = static_cast<double>(R.M[SwapAxis[Col] * 3 + SwapAxis[Row]]);
			}
		}

		return Result;
	}

	FQuat OrientationToWorldQuat(int32 Orientation)
	{
		// FQuat(FMatrix) は回転成分だけを取り出す。OrientationToWorldMatrix は
		// 行列式 +1・スケール 1 の純粋な回転なのでそのまま渡せる
		return FQuat(OrientationToWorldMatrix(Orientation));
	}
}
