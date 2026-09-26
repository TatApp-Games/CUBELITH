// WebMock/tests/generate.test.ts の maxPieces / generatePuzzle / generatePieces・solutionPlacements の正常系の移植
// 移植しないテスト: maxPieces の「範囲外の N は RangeError」（checkf で停止するため）
// 移植しないテスト: generatePuzzle の「不正な N は RangeError」（checkf で停止するため）
// 移植しないテスト: generatePuzzle の「不正な M は RangeError」（checkf で停止するため）
// 移植しないテスト: generatePuzzle の「不正なシードは RangeError」（checkf で停止するため）
// 「不正なシード（非整数）」は C++ では型で排除される（シードを uint32 で受けるので整数しか渡せない）
// 006 で足すテスト: scatterPlacements と scatterPlacements のオプション（ScatterPlacements が要る）
// 組み合わせを回すテストは、一致しているときは記録を増やさず、食い違ったときだけ AddError する

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Generate.h"
#include "Grid.h"
#include "Piece.h"

namespace CubelithCoreTests
{
	// GenerateTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace GenerateTestDetail
	{
		/** 試す空間サイズ（TS の SIZES） */
		const TArray<int32>& Sizes()
		{
			static const TArray<int32> Values{ 3, 4, 5, 6, 7 };
			return Values;
		}

		/** 試すシード（TS の SEEDS） */
		const TArray<uint32>& Seeds()
		{
			static const TArray<uint32> Values{ 1, 7, 12345, 2026 };
			return Values;
		}

		/** M の代表値（下限・中間・上限。TS の representativePieceCounts）。重複は取り除く */
		TArray<int32> RepresentativePieceCounts(int32 N)
		{
			const int32 Limit = Cubelith::MaxPieces(N);
			const int32 Middle = (Cubelith::MinPieceCount + Limit) / 2;

			TArray<int32> Result;
			for (const int32 Value : { Cubelith::MinPieceCount, Middle, Limit })
			{
				Result.AddUnique(Value);
			}
			return Result;
		}

		/** ボクセル集合が 6 近傍で連結しているか（BFS。TS の isConnected） */
		bool IsConnected(const TArray<Cubelith::FVec3>& Voxels)
		{
			if (Voxels.Num() == 0)
			{
				return false;
			}

			static const TArray<Cubelith::FVec3> Neighbors{
				Cubelith::Vec3(1, 0, 0), Cubelith::Vec3(-1, 0, 0), Cubelith::Vec3(0, 1, 0),
				Cubelith::Vec3(0, -1, 0), Cubelith::Vec3(0, 0, 1), Cubelith::Vec3(0, 0, -1) };

			const TSet<Cubelith::FVec3> All(Voxels);
			TSet<Cubelith::FVec3> Seen;
			Seen.Add(Voxels[0]);
			TArray<Cubelith::FVec3> Queue{ Voxels[0] };
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const Cubelith::FVec3 V = Queue[Head];
				for (const Cubelith::FVec3& Offset : Neighbors)
				{
					const Cubelith::FVec3 Neighbor = Cubelith::AddVec3(V, Offset);
					if (!All.Contains(Neighbor) || Seen.Contains(Neighbor))
					{
						continue;
					}
					Seen.Add(Neighbor);
					Queue.Add(Neighbor);
				}
			}
			return Seen.Num() == All.Num();
		}

		/** ピースが一致するか（id とボクセルの並び） */
		bool PieceEquals(const Cubelith::FPiece& A, const Cubelith::FPiece& B)
		{
			return A.Id == B.Id && A.Voxels == B.Voxels;
		}

		/** ピースの配列が一致するか */
		bool PiecesEqual(const TArray<Cubelith::FPiece>& A, const TArray<Cubelith::FPiece>& B)
		{
			if (A.Num() != B.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < A.Num(); ++Index)
			{
				if (!PieceEquals(A[Index], B[Index]))
				{
					return false;
				}
			}
			return true;
		}

		/** 生成結果が完全に一致するか（TS の toEqual に当たる比較） */
		bool PuzzleEquals(const Cubelith::FGeneratedPuzzle& A, const Cubelith::FGeneratedPuzzle& B)
		{
			return A.N == B.N && A.M == B.M && A.Seed == B.Seed &&
				PiecesEqual(A.Pieces, B.Pieces) && A.Solution == B.Solution;
		}

		/** N / M / シードの組み合わせをメッセージに出す */
		FString Describe(int32 N, int32 M, uint32 Seed)
		{
			return FString::Printf(TEXT("N=%d / M=%d / seed=%u"), N, M, Seed);
		}

		/**
		 * 1 つの組み合わせについて「全ボクセルがちょうど 1 つのピースに属し、各ピースが連結」を確かめる。
		 * 食い違ったら AddError して false（その組み合わせの残りは見ない）
		 */
		bool CheckPartition(FAutomationTestBase& Test, int32 N, int32 M, uint32 Seed)
		{
			const FString What = Describe(N, M, Seed);
			const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(N, M, Seed);

			if (Puzzle.N != N || Puzzle.M != M || Puzzle.Seed != Seed)
			{
				Test.AddError(FString::Printf(TEXT("%s: 生成結果の N / M / シードが引数と違う（%d / %d / %u）"),
					*What, Puzzle.N, Puzzle.M, Puzzle.Seed));
				return false;
			}
			if (Puzzle.Pieces.Num() != M || Puzzle.Solution.Num() != M)
			{
				Test.AddError(FString::Printf(TEXT("%s: 件数が M でない（pieces %d / solution %d）"),
					*What, Puzzle.Pieces.Num(), Puzzle.Solution.Num()));
				return false;
			}

			TMap<Cubelith::FVec3, int32> Occurrence;
			Occurrence.Reserve(N * N * N);
			for (int32 Index = 0; Index < M; ++Index)
			{
				const Cubelith::FPiece& Piece = Puzzle.Pieces[Index];
				const Cubelith::FPlacement& Placement = Puzzle.Solution[Index];

				// ピース id は 0..M-1
				if (Piece.Id != Index)
				{
					Test.AddError(FString::Printf(TEXT("%s: pieces[%d].id が %d（期待 %d）"), *What, Index, Piece.Id, Index));
					return false;
				}
				// 局所原点が (0,0,0) に正規化されている
				if (!Piece.Voxels.Contains(Cubelith::Vec3(0, 0, 0)))
				{
					Test.AddError(FString::Printf(TEXT("%s: pieces[%d] に局所原点 (0,0,0) が無い"), *What, Index));
					return false;
				}
				// 解答配置は向きが恒等
				if (Placement.Orientation != Cubelith::IdentityOrientation)
				{
					Test.AddError(FString::Printf(TEXT("%s: solution[%d] の向きが %d（期待 0）"), *What, Index, Placement.Orientation));
					return false;
				}

				const TArray<Cubelith::FVec3> World = Cubelith::PlacedVoxels(Piece, Placement);
				if (World.Num() == 0)
				{
					Test.AddError(FString::Printf(TEXT("%s: pieces[%d] のボクセルが空"), *What, Index));
					return false;
				}
				// 6 近傍で連結している
				if (!IsConnected(World))
				{
					Test.AddError(FString::Printf(TEXT("%s: pieces[%d] が連結していない"), *What, Index));
					return false;
				}

				for (const Cubelith::FVec3& V : World)
				{
					// 解答位置は N×N×N の中に収まる
					if (V.X < 0 || V.X >= N || V.Y < 0 || V.Y >= N || V.Z < 0 || V.Z >= N)
					{
						Test.AddError(FString::Printf(TEXT("%s: pieces[%d] の解答位置 (%d,%d,%d) が立方体の外"),
							*What, Index, V.X, V.Y, V.Z));
						return false;
					}
					++Occurrence.FindOrAdd(V, 0);
				}
			}

			// 欠けなし（N³ マスすべて）・重複なし（どのマスもちょうど 1 回）
			const int32 Total = N * N * N;
			if (Occurrence.Num() != Total)
			{
				Test.AddError(FString::Printf(TEXT("%s: 埋まったマスが %d 個（期待 %d 個）"), *What, Occurrence.Num(), Total));
				return false;
			}
			for (const TPair<Cubelith::FVec3, int32>& Pair : Occurrence)
			{
				if (Pair.Value != 1)
				{
					Test.AddError(FString::Printf(TEXT("%s: マス (%d,%d,%d) が %d 個のピースに属している"),
						*What, Pair.Key.X, Pair.Key.Y, Pair.Key.Z, Pair.Value));
					return false;
				}
			}

			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateMaxPiecesMatchesRuleTest, "CUBELITH.Core.Generate.MaxPiecesMatchesRule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateMaxPiecesMatchesRuleTest::RunTest(const FString& Parameters)
{
	// RULES.md 3.1 の上限 N + 4(N−2) に一致する
	TestEqual(TEXT("MaxPieces(3)"), Cubelith::MaxPieces(3), 7);
	TestEqual(TEXT("MaxPieces(4)"), Cubelith::MaxPieces(4), 12);
	TestEqual(TEXT("MaxPieces(5)"), Cubelith::MaxPieces(5), 17);
	TestEqual(TEXT("MaxPieces(6)"), Cubelith::MaxPieces(6), 22);
	TestEqual(TEXT("MaxPieces(7)"), Cubelith::MaxPieces(7), 27);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateMaxPiecesWithinBoundsTest, "CUBELITH.Core.Generate.MaxPiecesWithinBounds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateMaxPiecesWithinBoundsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// どの N でも下限 2 以上で N³ 以下
	for (const int32 N : GenerateTestDetail::Sizes())
	{
		const int32 Limit = Cubelith::MaxPieces(N);
		TestTrue(FString::Printf(TEXT("MaxPieces(%d)=%d が下限 %d 以上"), N, Limit, Cubelith::MinPieceCount),
			Limit >= Cubelith::MinPieceCount);
		TestTrue(FString::Printf(TEXT("MaxPieces(%d)=%d が N³ 以下"), N, Limit), Limit <= N * N * N);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGeneratePartitionsCubeTest, "CUBELITH.Core.Generate.PartitionsCube",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGeneratePartitionsCubeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// N × M の代表値 × シードの全組み合わせで「全ボクセルがちょうど 1 つのピースに属し、各ピースが連結」
	int32 Checked = 0;
	for (const int32 N : GenerateTestDetail::Sizes())
	{
		for (const int32 M : GenerateTestDetail::RepresentativePieceCounts(N))
		{
			for (const uint32 Seed : GenerateTestDetail::Seeds())
			{
				GenerateTestDetail::CheckPartition(*this, N, M, Seed);
				++Checked;
			}
		}
	}

	// 5 サイズ × M の代表値 3 通り × シード 4 個
	TestEqual(TEXT("確かめた組み合わせの数"), Checked, 60);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateDeterministicTest, "CUBELITH.Core.Generate.Deterministic",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 同じ引数なら結果が完全に一致する（決定的）
	for (const int32 N : GenerateTestDetail::Sizes())
	{
		for (const int32 M : GenerateTestDetail::RepresentativePieceCounts(N))
		{
			const bool bEquals = GenerateTestDetail::PuzzleEquals(
				Cubelith::GeneratePuzzle(N, M, 4649), Cubelith::GeneratePuzzle(N, M, 4649));
			if (!bEquals)
			{
				AddError(FString::Printf(TEXT("%s: 2 回の生成結果が違う"), *GenerateTestDetail::Describe(N, M, 4649)));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateDifferentSeedDiffersTest, "CUBELITH.Core.Generate.DifferentSeedDiffers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateDifferentSeedDiffersTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// シードが違えば別の分割になる
	TestFalse(TEXT("seed=1 と seed=2 の生成結果が違う"),
		GenerateTestDetail::PuzzleEquals(Cubelith::GeneratePuzzle(5, 8, 1), Cubelith::GeneratePuzzle(5, 8, 2)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGeneratePiecesMatchPuzzleTest, "CUBELITH.Core.Generate.PiecesMatchPuzzle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGeneratePiecesMatchPuzzleTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// GeneratePieces / SolutionPlacements は GeneratePuzzle と同じものを返す
	for (const int32 N : GenerateTestDetail::Sizes())
	{
		const TArray<int32> Counts = GenerateTestDetail::RepresentativePieceCounts(N);
		const int32 M = Counts.IsValidIndex(1) ? Counts[1] : Cubelith::MinPieceCount;
		const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(N, M, 31);

		TestTrue(FString::Printf(TEXT("%s: GeneratePieces が GeneratePuzzle と同じ"), *GenerateTestDetail::Describe(N, M, 31)),
			GenerateTestDetail::PiecesEqual(Cubelith::GeneratePieces(N, M, 31), Puzzle.Pieces));
		TestTrue(FString::Printf(TEXT("%s: SolutionPlacements が GeneratePuzzle と同じ"), *GenerateTestDetail::Describe(N, M, 31)),
			Cubelith::SolutionPlacements(N, M, 31) == Puzzle.Solution);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateReturnsCopyTest, "CUBELITH.Core.Generate.ReturnsCopy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateReturnsCopyTest::RunTest(const FString& Parameters)
{
	// 返した配列を書き換えても次の呼び出しに影響しない
	TArray<Cubelith::FPiece> Pieces = Cubelith::GeneratePieces(3, 4, 5);
	TestEqual(TEXT("最初の呼び出しの件数"), Pieces.Num(), 4);
	Pieces.RemoveAt(Pieces.Num() - 1);

	TestEqual(TEXT("書き換えた後の呼び出しの件数"), Cubelith::GeneratePieces(3, 4, 5).Num(), 4);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
