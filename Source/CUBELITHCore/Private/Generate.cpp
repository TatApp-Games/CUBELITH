#include "Generate.h"

#include "Algo/StableSort.h"
#include "Rng.h"
#include "Solve.h"

// 解釈: TS の at()（範囲外アクセスを例外にするヘルパ）は noUncheckedIndexedAccess 対策なので移植しない。
// C++ では TArray の operator[] がデバッグビルドで添字を検査する。

namespace Cubelith
{
	namespace
	{
		/** 未割り当てのボクセルを表す Owner の値（TS の UNASSIGNED） */
		constexpr int32 Unassigned = -1;

		/**
		 * 6 近傍のオフセット（TS の NEIGHBOR_OFFSETS）。
		 * 候補を積む順が以降の乱数の当たり先を決めるので、並び（+X, -X, +Y, -Y, +Z, -Z）を変えない。
		 */
		constexpr FVec3 NeighborOffsets[] = {
			{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

		/** N が 3..7 であることを確かめる（TS の assertSpaceSize）。整数かどうかは C++ では型で決まる */
		void AssertSpaceSize(int32 N)
		{
			checkf(N >= MinSpaceSize && N <= MaxSpaceSize, TEXT("空間サイズ N は %d..%d の整数 (got %d)"),
				MinSpaceSize, MaxSpaceSize, N);
		}

		/** M が 2..MaxPieces(N) であることを確かめる（TS の assertPieceCount） */
		void AssertPieceCount(int32 N, int32 M)
		{
			const int32 Limit = MaxPieces(N);
			checkf(M >= MinPieceCount && M <= Limit, TEXT("分割数 M は %d..%d の整数 (N=%d, got %d)"),
				MinPieceCount, Limit, N, M);
		}

		/** グリッド座標 → 一次元インデックス（TS の encodeIndex） */
		int32 EncodeIndex(const FVec3& V, int32 N)
		{
			return (V.X * N + V.Y) * N + V.Z;
		}

		/**
		 * 一次元インデックス → グリッド座標（TS の decodeIndex）。
		 * C++ の整数の除算は 0 方向への切り捨てで TS の Math.floor と違うが、Index は常に非負なのでそのままでよい。
		 */
		FVec3 DecodeIndex(int32 Index, int32 N)
		{
			return Vec3(Index / (N * N), (Index / N) % N, Index % N);
		}

		/** 座標が N×N×N の中にあるか（TS の isInside） */
		bool IsInside(const FVec3& V, int32 N)
		{
			return V.X >= 0 && V.X < N && V.Y >= 0 && V.Y < N && V.Z >= 0 && V.Z < N;
		}

		/** 散らしで 1 ピースを置くときのリトライ上限（TS の SCATTER_ATTEMPT_LIMIT） */
		constexpr int32 ScatterAttemptLimit = 10000;

		/** 散らしで範囲を広げる間隔（TS の SCATTER_WIDEN_INTERVAL） */
		constexpr int32 ScatterWidenInterval = 100;

		/** 散らし全体（クリア状態を避けるための引き直し）の上限（TS の SCATTER_RETRY_LIMIT） */
		constexpr int32 ScatterRetryLimit = 8;

		/**
		 * Keep をピース id 引きの表にする（TS の buildKeepMap）。未知の id / 重複した id は呼び出し側のバグなので checkf。
		 * 引くだけの用途なので TMap でよい（この表を回って順序に依存することはしない）。
		 */
		TMap<int32, FPlacement> BuildKeepMap(TArrayView<const FPiece> Pieces, TArrayView<const FPlacement> Keep)
		{
			TMap<int32, FPlacement> Map;
			if (Keep.Num() == 0)
			{
				return Map;
			}

			TSet<int32> Known;
			Known.Reserve(Pieces.Num());
			for (const FPiece& Piece : Pieces)
			{
				Known.Add(Piece.Id);
			}

			Map.Reserve(Keep.Num());
			for (const FPlacement& Placement : Keep)
			{
				checkf(Known.Contains(Placement.PieceId), TEXT("散らし: keep に未知のピース id %d"), Placement.PieceId);
				checkf(!Map.Contains(Placement.PieceId), TEXT("散らし: keep のピース id %d が重複している"), Placement.PieceId);
				Map.Add(Placement.PieceId, Placement);
			}
			return Map;
		}

		/**
		 * 1 回分の散らし（TS の scatterOnce）。重なりが出たらその場でリトライし、詰まったら範囲を広げる。
		 * KeepById にある配置はそのまま採用し、そのボクセルは先に占有として登録する。
		 *
		 * 乱数の消費の順が結果を決めるので（RULES.md 3.6）、TS の手順から 1 つも増やさず減らさない。
		 */
		TArray<FPlacement> ScatterOnce(TArrayView<const FPiece> Pieces, int32 N, FRng& Rng, bool bAllowRotation,
			const TMap<int32, FPlacement>& KeepById)
		{
			// 「含むかどうか」だけを見る集合なので TSet でよい（TS は Set<string>）
			TSet<FVec3> Occupied;
			TArray<FPlacement> Placements;
			Placements.Reserve(Pieces.Num());
			// 立方体は [0, N-1]³ にあるので、その中心のまわり ±(N+2) を既定の散らし範囲にする。
			// N >= 3 なので TS の Math.floor((n - 1) / 2) は整数除算でよい
			const int32 Center = (N - 1) / 2;

			// 固定ピースのボクセルを先に占有として登録し、残りがそこへ重ならないようにする
			for (const FPiece& Piece : Pieces)
			{
				const FPlacement* Kept = KeepById.Find(Piece.Id);
				if (Kept == nullptr)
				{
					continue;
				}
				for (const FVec3& V : PlacedVoxels(Piece, *Kept))
				{
					Occupied.Add(V);
				}
			}

			for (const FPiece& Piece : Pieces)
			{
				const FPlacement* Kept = KeepById.Find(Piece.Id);
				if (Kept != nullptr)
				{
					// そのまま残す。乱数も消費しないので keep の内容が同じなら結果も同じ
					Placements.Add(*Kept);
					continue;
				}

				int32 Half = N + 2;
				int32 Attempts = 0;
				for (;;)
				{
					const int32 Span = Half * 2 + 1;
					FPlacement Placement;
					Placement.PieceId = Piece.Id;
					// bAllowRotation が false のときは向きの乱数を引かない（引くと以降の列が全部ずれる）
					Placement.Orientation = bAllowRotation ? Rng.NextInt(OrientationCount) : IdentityOrientation;
					// 引く順は X → Y → Z。C++ は関数の引数の評価順が決まらないので、Vec3(...) の中に直接書かず先に受ける
					const int32 X = Center - Half + Rng.NextInt(Span);
					const int32 Y = Center - Half + Rng.NextInt(Span);
					const int32 Z = Center - Half + Rng.NextInt(Span);
					Placement.Position = Vec3(X, Y, Z);

					const TArray<FVec3> Voxels = PlacedVoxels(Piece, Placement);
					bool bFree = true;
					for (const FVec3& V : Voxels)
					{
						if (Occupied.Contains(V))
						{
							bFree = false;
							break;
						}
					}
					if (bFree)
					{
						for (const FVec3& V : Voxels)
						{
							Occupied.Add(V);
						}
						Placements.Add(Placement);
						break;
					}

					++Attempts;
					checkf(Attempts < ScatterAttemptLimit, TEXT("散らしに失敗: ピース %d を置けなかった"), Piece.Id);
					// 詰まってきたら範囲を広げ、必ず終わるようにする
					if (Attempts % ScatterWidenInterval == 0)
					{
						++Half;
					}
				}
			}
			return Placements;
		}
	}

	int32 MaxPieces(int32 N)
	{
		AssertSpaceSize(N);
		return 5 * N - 8;
	}

	FGeneratedPuzzle GeneratePuzzle(int32 N, int32 M, uint32 Seed)
	{
		AssertSpaceSize(N);
		AssertPieceCount(N, M);
		// TS の assertSeed（シードが整数か）に当たる検査は要らない。シードを uint32 で受けるので型で排除される

		FRng Rng = CreateRng(Seed);
		const int32 Total = N * N * N;
		// Owner[Index] = そのボクセルを持つピース id（未割り当ては Unassigned）
		TArray<int32> Owner;
		Owner.Init(Unassigned, Total);
		// Members[Piece] = そのピースのボクセルのインデックス
		TArray<TArray<int32>> Members;
		Members.SetNum(M);
		// Frontiers[Piece] = 吸収候補（隣接する未割り当てボクセル）。割り当て済みの古い候補も混ざる
		TArray<TArray<int32>> Frontiers;
		Frontiers.SetNum(M);

		/** ボクセルをピースに吸収し、その 6 近傍の空きを候補に積む（TS の absorb） */
		const auto Absorb = [&Owner, &Members, &Frontiers, N](int32 Index, int32 Piece) -> void
		{
			Owner[Index] = Piece;
			Members[Piece].Add(Index);
			const FVec3 V = DecodeIndex(Index, N);
			TArray<int32>& Frontier = Frontiers[Piece];
			for (const FVec3& Offset : NeighborOffsets)
			{
				const FVec3 Neighbor = AddVec3(V, Offset);
				if (!IsInside(Neighbor, N))
				{
					continue;
				}
				const int32 NeighborIndex = EncodeIndex(Neighbor, N);
				if (Owner[NeighborIndex] != Unassigned)
				{
					continue;
				}
				Frontier.Add(NeighborIndex);
			}
		};

		// 2. 相異なるランダム座標に M 個のシードを置く。
		//    部分 Fisher-Yates で先頭 M 個を選ぶので、重複のリトライ無しに必ず相異なる
		TArray<int32> Shuffled;
		Shuffled.Reserve(Total);
		for (int32 Index = 0; Index < Total; ++Index)
		{
			Shuffled.Add(Index);
		}
		for (int32 I = 0; I < M; ++I)
		{
			const int32 J = I + Rng.NextInt(Total - I);
			const int32 A = Shuffled[I];
			const int32 B = Shuffled[J];
			Shuffled[I] = B;
			Shuffled[J] = A;
			Absorb(B, I);
		}

		// 3. 各ピースが隣接する空きを 1 つ吸収する、をラウンドロビンで繰り返す
		int32 Assigned = M;
		while (Assigned < Total)
		{
			bool bGrew = false;
			for (int32 Piece = 0; Piece < M && Assigned < Total; ++Piece)
			{
				TArray<int32>& Frontier = Frontiers[Piece];
				int32 Picked = Unassigned;
				while (Frontier.Num() > 0)
				{
					const int32 K = Rng.NextInt(Frontier.Num());
					const int32 Candidate = Frontier[K];
					// TS と同じ swap-remove（末尾を K に移して末尾を落とす）。
					// この取り出し方が以降の乱数の当たり先を決めるので変えない
					Frontier[K] = Frontier.Last();
					Frontier.RemoveAt(Frontier.Num() - 1);
					if (Owner[Candidate] == Unassigned)
					{
						Picked = Candidate;
						break;
					}
				}
				// 空きが無いピースはこのラウンドをスキップ
				if (Picked == Unassigned)
				{
					continue;
				}
				Absorb(Picked, Piece);
				++Assigned;
				bGrew = true;
			}
			if (bGrew)
			{
				continue;
			}

			// すべてのピースがスキップになったのに空きが残る場合（RULES.md 3.2-3 の孤立した空き）。
			// 候補は吸収されるまで Frontier に残る作りなので実際にはここへ来ないが、仕様どおり保険を置く
			TArray<int32> Remaining;
			for (int32 Index = 0; Index < Total; ++Index)
			{
				if (Owner[Index] == Unassigned)
				{
					Remaining.Add(Index);
				}
			}
			while (Remaining.Num() > 0)
			{
				TArray<int32> Next;
				bool bProgressed = false;
				for (const int32 Index : Remaining)
				{
					const FVec3 V = DecodeIndex(Index, N);
					int32 Host = Unassigned;
					for (const FVec3& Offset : NeighborOffsets)
					{
						const FVec3 Neighbor = AddVec3(V, Offset);
						if (!IsInside(Neighbor, N))
						{
							continue;
						}
						const int32 NeighborOwner = Owner[EncodeIndex(Neighbor, N)];
						if (NeighborOwner != Unassigned)
						{
							Host = NeighborOwner;
							break;
						}
					}
					// どのピースにも隣接しない空きは次の周回に回す（周りが埋まれば吸収できる）
					if (Host == Unassigned)
					{
						Next.Add(Index);
						continue;
					}
					Absorb(Index, Host);
					++Assigned;
					bProgressed = true;
				}
				checkf(bProgressed, TEXT("生成に失敗: どのピースにも隣接しない空きが残った"));
				Remaining = MoveTemp(Next);
			}
		}

		// 4. 解答位置（絶対座標）から局所座標へ正規化し、正規化で引いたオフセットを解答配置にする
		FGeneratedPuzzle Puzzle;
		Puzzle.N = N;
		Puzzle.M = M;
		Puzzle.Seed = Seed;
		Puzzle.Pieces.Reserve(M);
		Puzzle.Solution.Reserve(M);
		for (int32 Id = 0; Id < M; ++Id)
		{
			TArray<FVec3> Absolute;
			Absolute.Reserve(Members[Id].Num());
			for (const int32 Index : Members[Id])
			{
				Absolute.Add(DecodeIndex(Index, N));
			}
			// JS の Array.prototype.sort は安定ソート（Docs/SPEC_UE.md 7.1）。
			// CompareVec3 は相異なる座標の全順序なので、結果の並びは一意に決まる
			Algo::StableSort(Absolute, [](const FVec3& A, const FVec3& B) -> bool
			{
				return CompareVec3(A, B) < 0;
			});

			Puzzle.Pieces.Add(CreatePiece(Id, Absolute));

			FPlacement Placement;
			Placement.PieceId = Id;
			Placement.Orientation = IdentityOrientation;
			Placement.Position = LocalOrigin(Absolute);
			Puzzle.Solution.Add(Placement);
		}

		return Puzzle;
	}

	TArray<FPiece> GeneratePieces(int32 N, int32 M, uint32 Seed)
	{
		// 呼び出しごとに作り直すので、返した配列を書き換えても次の呼び出しには影響しない（TS の .slice()）
		FGeneratedPuzzle Puzzle = GeneratePuzzle(N, M, Seed);
		return MoveTemp(Puzzle.Pieces);
	}

	TArray<FPlacement> SolutionPlacements(int32 N, int32 M, uint32 Seed)
	{
		FGeneratedPuzzle Puzzle = GeneratePuzzle(N, M, Seed);
		return MoveTemp(Puzzle.Solution);
	}

	TArray<FPlacement> ScatterPlacements(TArrayView<const FPiece> Pieces, int32 N, uint32 Seed, const FScatterOptions& Options)
	{
		AssertSpaceSize(N);
		// TS の assertSeed（シードが整数か）に当たる検査は要らない。シードを uint32 で受けるので型で排除される
		checkf(Pieces.Num() > 0, TEXT("散らし: ピースが空"));

		const TMap<int32, FPlacement> KeepById = BuildKeepMap(Pieces, Options.Keep);

		// 呼び出しごとに作り直すので、散らしと散らし直しは互いに乱数を食い合わせない（Docs/FIXTURES.md）
		FRng Rng = CreateRng(Seed);
		TArray<FPlacement> Last;
		bool bHasLast = false;
		// 引き直しは同じ Rng を使い続ける（乱数の続きを消費する）
		for (int32 Retry = 0; Retry < ScatterRetryLimit; ++Retry)
		{
			TArray<FPlacement> Placements = ScatterOnce(Pieces, N, Rng, Options.bAllowRotation, KeepById);
			if (!IsSolved(Pieces, Placements, N))
			{
				return Placements;
			}
			Last = MoveTemp(Placements);
			bHasLast = true;
		}

		checkf(KeepById.Num() > 0 && bHasLast, TEXT("散らしに失敗: クリア状態でない配置を作れなかった"));
		return Last;
	}
}
