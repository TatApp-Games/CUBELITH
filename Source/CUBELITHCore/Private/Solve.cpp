#include "Solve.h"

#include "Algo/StableSort.h"

namespace Cubelith
{
	namespace
	{
		/**
		 * 候補にする平行移動の範囲（各軸 -1 / 0 / +1 の 27 通り。TS の SNAP_RANGE）。
		 * RULES.md 3.5 の「半マス〜1 マス以内」を、グリッド単位の実装では 1 マス以内と読む。
		 */
		constexpr int32 SnapRange = 1;

		/**
		 * 6 近傍のオフセット（TS の FACE_NEIGHBORS）。「少なくとも 1 面で接する」の判定に使う。
		 * FVec3 を Vec3() で作るので constexpr の配列にはできず、一度だけ作る static const で持つ。
		 */
		const TArray<FVec3>& FaceNeighbors()
		{
			static const TArray<FVec3> Neighbors{
				Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(0, 0, -1) };
			return Neighbors;
		}

		/** 原点からのマンハッタン距離（TS の manhattan）。候補の「近さ」の尺度 */
		int32 Manhattan(const FVec3& V)
		{
			return FMath::Abs(V.X) + FMath::Abs(V.Y) + FMath::Abs(V.Z);
		}

		/**
		 * 候補の平行移動を「近い順」に並べた表（TS の SNAP_OFFSETS）。
		 * マンハッタン距離の小さい順、同点は座標の辞書順（x → y → z）。
		 * 先頭から見て最初に条件を満たしたものを採れば「最も近いもの」を決定的に選べる。
		 * TS と同じく一度だけ作って使い回す。
		 */
		const TArray<FVec3>& SnapOffsets()
		{
			static const TArray<FVec3> Offsets = []() -> TArray<FVec3>
			{
				TArray<FVec3> Result;
				Result.Reserve((2 * SnapRange + 1) * (2 * SnapRange + 1) * (2 * SnapRange + 1));
				for (int32 X = -SnapRange; X <= SnapRange; ++X)
				{
					for (int32 Y = -SnapRange; Y <= SnapRange; ++Y)
					{
						for (int32 Z = -SnapRange; Z <= SnapRange; ++Z)
						{
							Result.Add(Vec3(X, Y, Z));
						}
					}
				}

				// TS は Array.prototype.sort（安定ソート）。比較関数が全順序なので結果は一意だが、
				// 約束どおり Algo::StableSort を使う（Docs/SPEC_UE.md 7.1）
				Algo::StableSort(Result, [](const FVec3& A, const FVec3& B) -> bool
				{
					const int32 Distance = Manhattan(A) - Manhattan(B);
					return Distance != 0 ? Distance < 0 : CompareVec3(A, B) < 0;
				});
				return Result;
			}();
			return Offsets;
		}
	}

	// 解釈: TS の voxelKey(x, y, z) は Set のキー用の内部関数なので移植しない。
	// C++ では FVec3 に operator== と GetTypeHash があるので TSet<FVec3> をそのまま使える
	// （grid.ts の vec3Key は公開 API なので Grid.h の Vec3Key として移植済み）。

	bool IsSolved(TArrayView<const FPiece> Pieces, TArrayView<const FPlacement> Placements, int32 N)
	{
		// 判定の順序は TS のまま保つ（読み合わせできるように）
		checkf(N >= 1, TEXT("空間サイズ N は 1 以上の整数 (got %d)"), N);

		// 引く表だけなので TMap でよい（順序は結果に効かない）
		TMap<int32, const FPiece*> ById;
		ById.Reserve(Pieces.Num());
		for (const FPiece& Piece : Pieces)
		{
			checkf(!ById.Contains(Piece.Id), TEXT("クリア判定: ピース id %d が重複している"), Piece.Id);
			ById.Add(Piece.Id, &Piece);
		}
		checkf(Placements.Num() == ById.Num(), TEXT("クリア判定: ピース数 %d と配置数 %d が一致しない"),
			ById.Num(), Placements.Num());

		// ボクセル総数が N³ でなければ、置き方によらずクリアにはなり得ない
		const int32 Total = N * N * N;
		int32 Count = 0;
		for (const FPiece& Piece : Pieces)
		{
			Count += Piece.Voxels.Num();
		}
		if (Count != Total)
		{
			return false;
		}

		// 各配置をワールドのグリッド座標に変換して集める
		TArray<FVec3> World;
		World.Reserve(Total);
		TSet<int32> Placed;
		Placed.Reserve(Placements.Num());
		for (const FPlacement& Placement : Placements)
		{
			const FPiece* const* Found = ById.Find(Placement.PieceId);
			checkf(Found != nullptr, TEXT("クリア判定: 未知のピース id %d"), Placement.PieceId);
			checkf(!Placed.Contains(Placement.PieceId), TEXT("クリア判定: ピース id %d の配置が重複している"),
				Placement.PieceId);
			Placed.Add(Placement.PieceId);
			World.Append(PlacedVoxels(**Found, Placement));
		}

		// 外接ボックスが N×N×N でなければ、はみ出しか隙間がある
		const FBoundingBox Box = BoundingBox(World);
		if (Box.Size.X != N || Box.Size.Y != N || Box.Size.Z != N)
		{
			return false;
		}

		// ここまで来れば全ボクセルは箱の中。重複が 1 つも無ければ N³ マスをちょうど埋めている
		TArray<bool> Seen;
		Seen.Init(false, Total);
		for (const FVec3& V : World)
		{
			const int32 Index = ((V.X - Box.Min.X) * N + (V.Y - Box.Min.Y)) * N + (V.Z - Box.Min.Z);
			if (Seen[Index])
			{
				return false;
			}
			Seen[Index] = true;
		}
		return true;
	}

	TOptional<FPlacement> SnapCandidate(
		TArrayView<const FPiece> Pieces, TArrayView<const FPlacement> Placements, int32 ActivePieceId, int32 N)
	{
		checkf(N >= 1, TEXT("空間サイズ N は 1 以上の整数 (got %d)"), N);

		TMap<int32, const FPiece*> ById;
		ById.Reserve(Pieces.Num());
		for (const FPiece& Piece : Pieces)
		{
			checkf(!ById.Contains(Piece.Id), TEXT("スナップ候補: ピース id %d が重複している"), Piece.Id);
			ById.Add(Piece.Id, &Piece);
		}

		// アクティブなピースの配置と、それ以外のピースのワールドボクセルを 1 度の走査で集める
		const FPlacement* Active = nullptr;
		TArray<FVec3> Others;
		TSet<int32> Seen;
		Seen.Reserve(Placements.Num());
		for (const FPlacement& Placement : Placements)
		{
			const FPiece* const* Found = ById.Find(Placement.PieceId);
			checkf(Found != nullptr, TEXT("スナップ候補: 未知のピース id %d"), Placement.PieceId);
			checkf(!Seen.Contains(Placement.PieceId), TEXT("スナップ候補: ピース id %d の配置が重複している"),
				Placement.PieceId);
			Seen.Add(Placement.PieceId);
			if (Placement.PieceId == ActivePieceId)
			{
				Active = &Placement;
				continue;
			}
			Others.Append(PlacedVoxels(**Found, Placement));
		}
		checkf(Active != nullptr, TEXT("スナップ候補: アクティブなピース id %d の配置が無い"), ActivePieceId);
		const FPiece* const* ActivePieceFound = ById.Find(ActivePieceId);
		checkf(ActivePieceFound != nullptr, TEXT("スナップ候補: 未知のピース id %d"), ActivePieceId);

		// 接する相手がいなければ条件 1 を満たしようがない
		if (Others.Num() == 0)
		{
			return TOptional<FPlacement>();
		}

		const TArray<FVec3> ActiveVoxels = PlacedVoxels(**ActivePieceFound, *Active);
		if (ActiveVoxels.Num() == 0)
		{
			return TOptional<FPlacement>();
		}

		// 相手のボクセルは候補ごとに変わらないので、集合と外接ボックスは 1 度だけ作って使い回す
		// （N=7 / M=27 でドラッグ中に毎回呼ばれても重くならないように）。
		// 集合は「含むかどうか」だけを見るので TSet でよい
		TSet<FVec3> Occupied;
		Occupied.Reserve(Others.Num());
		for (const FVec3& V : Others)
		{
			Occupied.Add(V);
		}
		const FBoundingBox OtherBox = BoundingBox(Others);
		// アクティブなピースの外接ボックスは平行移動で丸ごと動くだけなので、これも先に求めておく
		const FBoundingBox ActiveBox = BoundingBox(ActiveVoxels);

		/** 条件 3: 相手とアクティブを合わせた外接立方体が N×N×N に収まるか */
		const auto FitsInSpace = [&OtherBox, &ActiveBox, N](const FVec3& Offset) -> bool
		{
			const int32 SpanX =
				FMath::Max(OtherBox.Max.X, ActiveBox.Max.X + Offset.X) -
				FMath::Min(OtherBox.Min.X, ActiveBox.Min.X + Offset.X);
			const int32 SpanY =
				FMath::Max(OtherBox.Max.Y, ActiveBox.Max.Y + Offset.Y) -
				FMath::Min(OtherBox.Min.Y, ActiveBox.Min.Y + Offset.Y);
			const int32 SpanZ =
				FMath::Max(OtherBox.Max.Z, ActiveBox.Max.Z + Offset.Z) -
				FMath::Min(OtherBox.Min.Z, ActiveBox.Min.Z + Offset.Z);
			return SpanX + 1 <= N && SpanY + 1 <= N && SpanZ + 1 <= N;
		};

		/** 条件 1 と 2: 重ならず、かつどこかで 1 面接するか */
		const auto FitsAgainstOthers = [&Occupied, &ActiveVoxels](const FVec3& Offset) -> bool
		{
			bool bTouches = false;
			for (const FVec3& V : ActiveVoxels)
			{
				const FVec3 Moved = AddVec3(V, Offset);
				if (Occupied.Contains(Moved))
				{
					return false;
				}
				if (bTouches)
				{
					continue;
				}
				for (const FVec3& D : FaceNeighbors())
				{
					if (Occupied.Contains(AddVec3(Moved, D)))
					{
						bTouches = true;
						break;
					}
				}
			}
			return bTouches;
		};

		for (const FVec3& Offset : SnapOffsets())
		{
			// 判定は軽い順に。外接立方体だけなら比較 6 回で済む
			if (!FitsInSpace(Offset))
			{
				continue;
			}
			if (!FitsAgainstOthers(Offset))
			{
				continue;
			}

			FPlacement Result;
			Result.PieceId = ActivePieceId;
			Result.Orientation = Active->Orientation;
			Result.Position = AddVec3(Active->Position, Offset);
			return TOptional<FPlacement>(Result);
		}
		return TOptional<FPlacement>();
	}
}
