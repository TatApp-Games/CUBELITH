// CubelithProgress.h の実装。手順（面接触のグラフ → 連結成分 → 最大の塊 → 外接ボックスの判定）は
// 移植元 WebMock/src/ui/progress.ts から変えない

#include "CubelithProgress.h"

#include "Grid.h"

namespace Cubelith
{
	// このファイルだけで使う表。unity ビルドでは他の .cpp と同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace ProgressDetail
	{
		/** 6 近傍。「少なくとも 1 面で接する」の判定に使う（TS の FACE_NEIGHBORS） */
		const FVec3 FaceNeighbors[6] = {
			FVec3{ 1, 0, 0 },
			FVec3{ -1, 0, 0 },
			FVec3{ 0, 1, 0 },
			FVec3{ 0, -1, 0 },
			FVec3{ 0, 0, 1 },
			FVec3{ 0, 0, -1 },
		};
	}

	int32 UnsettledPieceCount(TArrayView<const FPiece> Pieces, TArrayView<const FPlacement> Placements, int32 N)
	{
		TMap<int32, const FPiece*> PieceById;
		PieceById.Reserve(Pieces.Num());
		for (const FPiece& Piece : Pieces)
		{
			PieceById.Add(Piece.Id, &Piece);
		}

		// 走査の起点は Placements の並びで回す（TS と同じく、最大の塊が同数なら先に見つかったほうを採る）
		TArray<int32> Ids;
		TMap<int32, TArray<FVec3>> VoxelsById;
		// TS は "x,y,z" の文字列キーの Map。FVec3 は GetTypeHash を持つのでそのままキーにできる（Grid.h）
		TMap<FVec3, int32> OwnerByVoxel;
		for (const FPlacement& Placement : Placements)
		{
			const FPiece* const* Found = PieceById.Find(Placement.PieceId);
			if (Found == nullptr)
			{
				continue;
			}
			TArray<FVec3> Voxels = PlacedVoxels(**Found, Placement);
			Ids.Add(Placement.PieceId);
			for (const FVec3& Voxel : Voxels)
			{
				// プレイ中はピースが重なり得る。その場合は先に置いたピースをそのマスの代表にする
				if (!OwnerByVoxel.Contains(Voxel))
				{
					OwnerByVoxel.Add(Voxel, Placement.PieceId);
				}
			}
			VoxelsById.Add(Placement.PieceId, MoveTemp(Voxels));
		}
		if (Ids.Num() == 0)
		{
			return 0;
		}

		// 面接触のグラフを作る
		TMap<int32, TSet<int32>> Neighbors;
		Neighbors.Reserve(Ids.Num());
		for (const int32 Id : Ids)
		{
			Neighbors.Add(Id, TSet<int32>());
		}
		for (const int32 Id : Ids)
		{
			for (const FVec3& Voxel : VoxelsById[Id])
			{
				for (const FVec3& Offset : ProgressDetail::FaceNeighbors)
				{
					const int32* Other = OwnerByVoxel.Find(AddVec3(Voxel, Offset));
					if (Other == nullptr || *Other == Id)
					{
						continue;
					}
					Neighbors[Id].Add(*Other);
					Neighbors[*Other].Add(Id);
				}
			}
		}

		// 連結成分を幅優先で列挙し、最大のものを本体とする（Component 自身をキューに使う）
		TSet<int32> Visited;
		TArray<int32> Largest;
		for (const int32 Id : Ids)
		{
			if (Visited.Contains(Id))
			{
				continue;
			}
			Visited.Add(Id);
			TArray<int32> Component;
			Component.Add(Id);
			for (int32 Head = 0; Head < Component.Num(); ++Head)
			{
				const int32 Current = Component[Head];
				for (const int32 Next : Neighbors[Current])
				{
					if (Visited.Contains(Next))
					{
						continue;
					}
					Visited.Add(Next);
					Component.Add(Next);
				}
			}
			if (Component.Num() > Largest.Num())
			{
				Largest = MoveTemp(Component);
			}
		}

		if (Largest.Num() < 2)
		{
			return Ids.Num();
		}

		TArray<FVec3> BodyVoxels;
		for (const int32 Id : Largest)
		{
			BodyVoxels.Append(VoxelsById[Id]);
		}
		const FBoundingBox Box = BoundingBox(BodyVoxels);
		if (Box.Size.X > N || Box.Size.Y > N || Box.Size.Z > N)
		{
			return Ids.Num();
		}
		return Ids.Num() - Largest.Num();
	}
}
