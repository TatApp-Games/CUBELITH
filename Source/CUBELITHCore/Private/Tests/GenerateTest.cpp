// WebMock/tests/generate.test.ts の maxPieces / generatePuzzle / generatePieces・solutionPlacements /
// scatterPlacements / scatterPlacements のオプション の正常系の移植
// 移植しないテスト: maxPieces の「範囲外の N は RangeError」（checkf で停止するため）
// 移植しないテスト: generatePuzzle の「不正な N は RangeError」（checkf で停止するため）
// 移植しないテスト: generatePuzzle の「不正な M は RangeError」（checkf で停止するため）
// 移植しないテスト: generatePuzzle の「不正なシードは RangeError」（checkf で停止するため）
// 移植しないテスト: scatterPlacements の「不正な引数は RangeError」（checkf で停止するため）
// 移植しないテスト: scatterPlacements のオプションの「keep の未知 id / 重複 id は例外」（checkf で停止するため）
// 「不正なシード（非整数）」は C++ では型で排除される（シードを uint32 で受けるので整数しか渡せない）
// 組み合わせを回すテストは、一致しているときは記録を増やさず、食い違ったときだけ AddError する

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Generate.h"
#include "Grid.h"
#include "Piece.h"
#include "Solve.h"

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

		/** 散らしで試す M（TS の [MIN_PIECE_COUNT, maxPieces(n)]）。N=3 でも 2 と 7 で重複しない */
		TArray<int32> ScatterPieceCounts(int32 N)
		{
			TArray<int32> Result;
			for (const int32 Value : { Cubelith::MinPieceCount, Cubelith::MaxPieces(N) })
			{
				Result.AddUnique(Value);
			}
			return Result;
		}

		/** ピースと配置を id で対応付けて、全ワールド座標を集める（TS の worldVoxels）。未知の id は数に出ない */
		TArray<Cubelith::FVec3> WorldVoxels(
			const TArray<Cubelith::FPiece>& Pieces, const TArray<Cubelith::FPlacement>& Placements)
		{
			TArray<Cubelith::FVec3> World;
			for (const Cubelith::FPlacement& Placement : Placements)
			{
				const Cubelith::FPiece* Piece = Pieces.FindByPredicate(
					[&Placement](const Cubelith::FPiece& Candidate) -> bool { return Candidate.Id == Placement.PieceId; });
				if (Piece == nullptr)
				{
					continue;
				}
				World.Append(Cubelith::PlacedVoxels(*Piece, Placement));
			}
			return World;
		}

		/** 配置を id で引く（TS の pick）。無ければ nullptr */
		const Cubelith::FPlacement* FindPlacement(const TArray<Cubelith::FPlacement>& Placements, int32 PieceId)
		{
			return Placements.FindByPredicate(
				[PieceId](const Cubelith::FPlacement& Placement) -> bool { return Placement.PieceId == PieceId; });
		}

		/** ワールド座標に重複が無いか（ピース同士が重なっていないか） */
		bool NoOverlap(const TArray<Cubelith::FVec3>& World)
		{
			const TSet<Cubelith::FVec3> Unique(World);
			return Unique.Num() == World.Num();
		}

		/** scatterPlacements のオプションのテストで使うパズル（TS の N / M / SEED と puzzle()） */
		constexpr int32 OptionsN = 4;
		constexpr int32 OptionsM = 5;
		constexpr uint32 OptionsSeed = 20260906;

		Cubelith::FGeneratedPuzzle OptionsPuzzle()
		{
			return Cubelith::GeneratePuzzle(OptionsN, OptionsM, OptionsSeed);
		}

		/**
		 * 1 つの散らしについて「重ならず、向きは 0..23、クリアではない」を確かめる。
		 * 食い違ったら AddError して false（その組み合わせの残りは見ない）
		 */
		bool CheckScatterLayout(FAutomationTestBase& Test, const TArray<Cubelith::FPiece>& Pieces,
			const TArray<Cubelith::FPlacement>& Placements, int32 N, int32 M, uint32 Seed)
		{
			const FString What = Describe(N, M, Seed);

			if (Placements.Num() != M)
			{
				Test.AddError(FString::Printf(TEXT("%s: 散らしの件数が %d（期待 %d）"), *What, Placements.Num(), M));
				return false;
			}

			// 全ピースがちょうど 1 回ずつ現れる（id を昇順に並べると 0..M-1）
			TArray<int32> Ids;
			Ids.Reserve(M);
			for (const Cubelith::FPlacement& Placement : Placements)
			{
				Ids.Add(Placement.PieceId);
			}
			Ids.Sort();
			for (int32 Index = 0; Index < M; ++Index)
			{
				if (Ids[Index] != Pieces[Index].Id)
				{
					Test.AddError(FString::Printf(TEXT("%s: 散らしのピース id を昇順にすると [%d] が %d（期待 %d）"),
						*What, Index, Ids[Index], Pieces[Index].Id));
					return false;
				}
			}

			// 向きは 0..23（位置が整数かどうかは C++ では型で決まるので見ない）
			for (const Cubelith::FPlacement& Placement : Placements)
			{
				if (Placement.Orientation < 0 || Placement.Orientation >= Cubelith::OrientationCount)
				{
					Test.AddError(FString::Printf(TEXT("%s: ピース %d の向きが %d（期待 0..%d）"),
						*What, Placement.PieceId, Placement.Orientation, Cubelith::OrientationCount - 1));
					return false;
				}
			}

			// ピース同士が重ならない（ワールド座標の重複が無い）
			const TArray<Cubelith::FVec3> World = WorldVoxels(Pieces, Placements);
			const int32 Total = N * N * N;
			if (World.Num() != Total || !NoOverlap(World))
			{
				Test.AddError(FString::Printf(TEXT("%s: 散らしたボクセルが %d 個 / 相異なる %d 個（期待 どちらも %d 個）"),
					*What, World.Num(), TSet<Cubelith::FVec3>(World).Num(), Total));
				return false;
			}

			// 散らした直後にクリアになっていない
			if (Cubelith::IsSolved(Pieces, Placements, N))
			{
				Test.AddError(FString::Printf(TEXT("%s: 散らした直後がクリア状態になっている"), *What));
				return false;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateScatterLayoutTest, "CUBELITH.Core.Generate.ScatterLayout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateScatterLayoutTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// N × M（下限・上限）× シード 2 個で「重ならず、向きは 0..23、クリアではない」
	int32 Checked = 0;
	for (const int32 N : GenerateTestDetail::Sizes())
	{
		for (const int32 M : GenerateTestDetail::ScatterPieceCounts(N))
		{
			for (const uint32 Seed : { 1u, 99u })
			{
				// TS と同じく分割のシードは Seed、散らしのシードは Seed + 1
				const TArray<Cubelith::FPiece> Pieces = Cubelith::GeneratePieces(N, M, Seed);
				const TArray<Cubelith::FPlacement> Placements = Cubelith::ScatterPlacements(Pieces, N, Seed + 1);
				GenerateTestDetail::CheckScatterLayout(*this, Pieces, Placements, N, M, Seed + 1);
				++Checked;
			}
		}
	}

	// 5 サイズ × M 2 通り × シード 2 個
	TestEqual(TEXT("確かめた組み合わせの数"), Checked, 20);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateScatterDeterministicTest, "CUBELITH.Core.Generate.ScatterDeterministic",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateScatterDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 同じ seed なら同じ配置（決定的）
	for (const int32 N : GenerateTestDetail::Sizes())
	{
		const int32 M = Cubelith::MaxPieces(N);
		const TArray<Cubelith::FPiece> Pieces = Cubelith::GeneratePieces(N, M, 3);
		const bool bEquals = Cubelith::ScatterPlacements(Pieces, N, 777) == Cubelith::ScatterPlacements(Pieces, N, 777);
		if (!bEquals)
		{
			AddError(FString::Printf(TEXT("%s: 2 回の散らしが違う"), *GenerateTestDetail::Describe(N, M, 777)));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateScatterDifferentSeedDiffersTest, "CUBELITH.Core.Generate.ScatterDifferentSeedDiffers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateScatterDifferentSeedDiffersTest::RunTest(const FString& Parameters)
{
	// seed が違えば別の配置になる
	const TArray<Cubelith::FPiece> Pieces = Cubelith::GeneratePieces(4, 6, 3);
	TestFalse(TEXT("seed=1 と seed=2 の散らしが違う"),
		Cubelith::ScatterPlacements(Pieces, 4, 1) == Cubelith::ScatterPlacements(Pieces, 4, 2));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateScatterWithinRangeTest, "CUBELITH.Core.Generate.ScatterWithinRange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateScatterWithinRangeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 散らす範囲は立方体の周囲 ±(N+2) 程度に収まる
	for (const int32 N : GenerateTestDetail::Sizes())
	{
		const int32 M = Cubelith::MaxPieces(N);
		const TArray<Cubelith::FPiece> Pieces = Cubelith::GeneratePieces(N, M, 11);
		const TArray<Cubelith::FPlacement> Placements = Cubelith::ScatterPlacements(Pieces, N, 12);
		const int32 Center = (N - 1) / 2;
		const int32 Half = N + 2;
		for (const Cubelith::FPlacement& Placement : Placements)
		{
			const Cubelith::FVec3& P = Placement.Position;
			if (FMath::Abs(P.X - Center) > Half || FMath::Abs(P.Y - Center) > Half || FMath::Abs(P.Z - Center) > Half)
			{
				AddError(FString::Printf(TEXT("%s: ピース %d の位置 (%d,%d,%d) が中心 %d の ±%d の外"),
					*GenerateTestDetail::Describe(N, M, 12), Placement.PieceId, P.X, P.Y, P.Z, Center, Half));
				break;
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateScatterDefaultOptionsTest, "CUBELITH.Core.Generate.ScatterDefaultOptions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateScatterDefaultOptionsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 省略時は従来どおり（3 引数の呼び出しと同じ結果）
	const Cubelith::FGeneratedPuzzle Puzzle = GenerateTestDetail::OptionsPuzzle();
	const TArray<Cubelith::FPlacement> Base =
		Cubelith::ScatterPlacements(Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed);

	TestTrue(TEXT("既定構築の FScatterOptions が 3 引数の呼び出しと同じ"),
		Cubelith::ScatterPlacements(Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed,
			Cubelith::FScatterOptions()) == Base);

	Cubelith::FScatterOptions Rotating;
	Rotating.bAllowRotation = true;
	TestTrue(TEXT("bAllowRotation = true が 3 引数の呼び出しと同じ"),
		Cubelith::ScatterPlacements(Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed,
			Rotating) == Base);

	// keep を空配列で明示しても省略と同じ（Docs/FIXTURES.md の reshuffleWithoutHints が scatter と同じになる理由）
	Cubelith::FScatterOptions EmptyKeep;
	EmptyKeep.Keep.Reset();
	TestTrue(TEXT("Keep が空でも 3 引数の呼び出しと同じ"),
		Cubelith::ScatterPlacements(Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed,
			EmptyKeep) == Base);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateScatterNoRotationTest, "CUBELITH.Core.Generate.ScatterNoRotation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateScatterNoRotationTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// bAllowRotation = false なら全配置の向きが IdentityOrientation
	Cubelith::FScatterOptions Options;
	Options.bAllowRotation = false;
	for (const int32 N : { 3, 4, 5 })
	{
		const TArray<Cubelith::FPiece> Pieces = Cubelith::GeneratePieces(N, 4, static_cast<uint32>(777 + N));
		const TArray<Cubelith::FPlacement> Placements = Cubelith::ScatterPlacements(Pieces, N, 999, Options);

		TestEqual(FString::Printf(TEXT("N=%d: 散らしの件数"), N), Placements.Num(), Pieces.Num());
		for (const Cubelith::FPlacement& Placement : Placements)
		{
			if (Placement.Orientation != Cubelith::IdentityOrientation)
			{
				AddError(FString::Printf(TEXT("N=%d: ピース %d の向きが %d（期待 %d）"),
					N, Placement.PieceId, Placement.Orientation, Cubelith::IdentityOrientation));
				break;
			}
		}
	}

	// bAllowRotation = false でもピース同士は重ならない
	const TArray<Cubelith::FPiece> Pieces = Cubelith::GeneratePieces(4, 6, 4242);
	const TArray<Cubelith::FPlacement> Placements = Cubelith::ScatterPlacements(Pieces, 4, 4242, Options);
	TestTrue(TEXT("bAllowRotation = false でもボクセルの重複が無い"),
		GenerateTestDetail::NoOverlap(GenerateTestDetail::WorldVoxels(Pieces, Placements)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateScatterKeepTest, "CUBELITH.Core.Generate.ScatterKeep",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateScatterKeepTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	const Cubelith::FGeneratedPuzzle Puzzle = GenerateTestDetail::OptionsPuzzle();

	// keep に渡した配置はそのまま返り、他のピースと重ならない
	for (const TArray<int32>& KeepIds : { TArray<int32>{ 0 }, TArray<int32>{ 0, 2 } })
	{
		Cubelith::FScatterOptions Options;
		for (const int32 KeepId : KeepIds)
		{
			Options.Keep.Add(Puzzle.Solution[KeepId]);
		}

		const TArray<Cubelith::FPlacement> Placements = Cubelith::ScatterPlacements(
			Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed, Options);

		// keep はそのまま含まれる
		for (const Cubelith::FPlacement& Kept : Options.Keep)
		{
			const Cubelith::FPlacement* Found = GenerateTestDetail::FindPlacement(Placements, Kept.PieceId);
			if (Found == nullptr || *Found != Kept)
			{
				AddError(FString::Printf(TEXT("keep %d 件: ピース %d の配置が keep のままになっていない"),
					KeepIds.Num(), Kept.PieceId));
				continue;
			}
		}

		// 固定ピースのボクセルに他のピースが重ならない
		const TSet<Cubelith::FVec3> KeptVoxels(GenerateTestDetail::WorldVoxels(Puzzle.Pieces, Options.Keep));
		TArray<Cubelith::FPlacement> Others;
		for (const Cubelith::FPlacement& Placement : Placements)
		{
			if (!KeepIds.Contains(Placement.PieceId))
			{
				Others.Add(Placement);
			}
		}
		for (const Cubelith::FVec3& V : GenerateTestDetail::WorldVoxels(Puzzle.Pieces, Others))
		{
			if (KeptVoxels.Contains(V))
			{
				AddError(FString::Printf(TEXT("keep %d 件: 固定ピースのボクセル (%d,%d,%d) に他のピースが重なっている"),
					KeepIds.Num(), V.X, V.Y, V.Z));
				break;
			}
		}
	}

	// keep 込みでも全ピースがちょうど 1 回ずつ現れ、ボクセルの重複が無い
	{
		Cubelith::FScatterOptions Options;
		Options.Keep.Add(Puzzle.Solution[1]);
		Options.Keep.Add(Puzzle.Solution[3]);

		const TArray<Cubelith::FPlacement> Placements = Cubelith::ScatterPlacements(
			Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed, Options);

		TestEqual(TEXT("keep 込みの散らしの件数"), Placements.Num(), Puzzle.Pieces.Num());
		// 並びは Pieces の並びに揃う（TS の placements.map(pieceId) === pieces.map(id)）
		for (int32 Index = 0; Index < Placements.Num(); ++Index)
		{
			if (Placements[Index].PieceId != Puzzle.Pieces[Index].Id)
			{
				AddError(FString::Printf(TEXT("keep 込み: placements[%d].pieceId が %d（期待 %d）"),
					Index, Placements[Index].PieceId, Puzzle.Pieces[Index].Id));
				break;
			}
		}
		TestTrue(TEXT("keep 込みでもボクセルの重複が無い"),
			GenerateTestDetail::NoOverlap(GenerateTestDetail::WorldVoxels(Puzzle.Pieces, Placements)));
	}

	// keep と bAllowRotation = false を同時に使える
	{
		Cubelith::FScatterOptions Options;
		Options.bAllowRotation = false;
		Options.Keep.Add(Puzzle.Solution[0]);

		const TArray<Cubelith::FPlacement> Placements = Cubelith::ScatterPlacements(
			Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed, Options);

		for (const Cubelith::FPlacement& Placement : Placements)
		{
			if (Placement.Orientation != Cubelith::IdentityOrientation)
			{
				AddError(FString::Printf(TEXT("keep + bAllowRotation = false: ピース %d の向きが %d（期待 %d）"),
					Placement.PieceId, Placement.Orientation, Cubelith::IdentityOrientation));
				break;
			}
		}
		const Cubelith::FPlacement* Found = GenerateTestDetail::FindPlacement(Placements, 0);
		TestTrue(TEXT("keep + bAllowRotation = false: ピース 0 が keep のまま"),
			Found != nullptr && *Found == Puzzle.Solution[0]);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateScatterOptionsDeterministicTest, "CUBELITH.Core.Generate.ScatterOptionsDeterministic",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateScatterOptionsDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	const Cubelith::FGeneratedPuzzle Puzzle = GenerateTestDetail::OptionsPuzzle();

	// 同じ引数の 2 回の呼び出しは等しい（決定的）
	Cubelith::FScatterOptions Fixed;
	Fixed.bAllowRotation = false;
	Fixed.Keep.Add(Puzzle.Solution[2]);
	TestTrue(TEXT("keep + bAllowRotation = false の 2 回が等しい"),
		Cubelith::ScatterPlacements(Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed, Fixed) ==
		Cubelith::ScatterPlacements(Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed, Fixed));

	Cubelith::FScatterOptions Rotated;
	Rotated.Keep.Add(Puzzle.Solution[2]);
	TestTrue(TEXT("keep だけの 2 回が等しい"),
		Cubelith::ScatterPlacements(Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed, Rotated) ==
		Cubelith::ScatterPlacements(Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed, Rotated));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGenerateScatterKeepAllTest, "CUBELITH.Core.Generate.ScatterKeepAll",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGenerateScatterKeepAllTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// keep が全ピース分でも checkf にならず、その配置がそのまま返る
	// 解答そのものはクリア状態だが、keep があるときは引き直しの上限を超えても例外にしない
	const Cubelith::FGeneratedPuzzle Puzzle = GenerateTestDetail::OptionsPuzzle();
	Cubelith::FScatterOptions Options;
	Options.Keep = Puzzle.Solution;

	TestTrue(TEXT("keep が全ピース分なら解答の配置がそのまま返る"),
		Cubelith::ScatterPlacements(Puzzle.Pieces, GenerateTestDetail::OptionsN, GenerateTestDetail::OptionsSeed, Options) ==
		Puzzle.Solution);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
