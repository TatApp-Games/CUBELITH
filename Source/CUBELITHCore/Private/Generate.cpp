#include "Generate.h"

#include "Algo/StableSort.h"
#include "Rng.h"

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
}
