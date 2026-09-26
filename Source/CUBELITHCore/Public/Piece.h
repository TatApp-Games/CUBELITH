// ピースの表現と、配置（向き + 位置）からワールド座標への変換。移植元は WebMock/src/core/piece.ts（RULES.md 3.2 / 3.4）
// 命名は 001 / 002 が決めた約束に揃える（namespace Cubelith・型は F 接頭辞・関数は WebMock と同じ名前を PascalCase に）
// 配列を受ける引数は TArrayView<const FVec3> で揃える（Grid.h の NearestOrientation と同じ形）

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Grid.h"

namespace Cubelith
{
	/**
	 * ピース（TS の Piece）。Voxels は局所座標のボクセル集合で、正規化後は局所原点が (0,0,0) になる。
	 * ボクセルの並びが照合データ（Docs/FIXTURES.md の pieces[].voxels）と比べる結果に効くので、
	 * TSet ではなく TArray で持ち、並びを勝手に変えない。
	 */
	struct CUBELITHCORE_API FPiece
	{
		int32 Id = 0;
		TArray<FVec3> Voxels;
	};

	/** 配置（TS の Placement）。ピースを Orientation で回してから Position へ平行移動する。 */
	struct CUBELITHCORE_API FPlacement
	{
		int32 PieceId = 0;
		int32 Orientation = IdentityOrientation;
		FVec3 Position;
	};

	inline bool operator==(const FPlacement& A, const FPlacement& B)
	{
		return A.PieceId == B.PieceId && A.Orientation == B.Orientation && A.Position == B.Position;
	}

	inline bool operator!=(const FPlacement& A, const FPlacement& B)
	{
		return !(A == B);
	}

	/** ボクセル集合の外接ボックス（TS の BoundingBox）。Size は含まれるマス数（Max - Min + 1）。 */
	struct CUBELITHCORE_API FBoundingBox
	{
		FVec3 Min;
		FVec3 Max;
		FVec3 Size;
	};

	/**
	 * 局所原点にするボクセルを返す（TS の localOrigin）。
	 * 解釈: RULES.md 3.3 は「最初のボクセル、または重心に最も近いボクセル」と選択肢を示しているが、
	 * 回転しても見た目の中心がずれにくい後者を採る。同点のときは座標の辞書順で最小のものを選び決定的にする。
	 * Voxels が空なら checkf（TS は RangeError）。
	 */
	CUBELITHCORE_API FVec3 LocalOrigin(TArrayView<const FVec3> Voxels);

	/** 局所原点が (0,0,0) になるよう平行移動したピースを返す（TS の normalizePiece）。ボクセルの並びは変えない。 */
	CUBELITHCORE_API FPiece NormalizePiece(const FPiece& Piece);

	/** id とボクセル集合から正規化済みのピースを作る（TS の createPiece）。 */
	CUBELITHCORE_API FPiece CreatePiece(int32 Id, TArrayView<const FVec3> Voxels);

	/**
	 * 配置をワールドのボクセル座標に変換する（TS の placedVoxels）。向きを適用してから Position を加算する。
	 * 並びは Piece.Voxels の並びのまま。Piece.Id と Placement.PieceId が食い違っていたら checkf（TS は Error）。
	 */
	CUBELITHCORE_API TArray<FVec3> PlacedVoxels(const FPiece& Piece, const FPlacement& Placement);

	/** ボクセル集合の外接ボックス（TS の boundingBox）。クリア判定（RULES.md 3.4）で使う。空なら checkf。 */
	CUBELITHCORE_API FBoundingBox BoundingBox(TArrayView<const FVec3> Voxels);
}
