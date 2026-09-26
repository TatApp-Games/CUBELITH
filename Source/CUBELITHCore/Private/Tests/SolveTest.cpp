// WebMock/tests/solve.test.ts の isSolved（手作りの小さな例・生成結果の解答配置）の正常系の移植
// 移植しないテスト: 「不正な N は RangeError」（checkf で停止するため）
// 移植しないテスト: 「ピースと配置の対応が壊れていたら Error」（checkf で停止するため）
// 組み合わせを回すテストは、一致しているときは記録を増やさず、食い違ったときだけ AddError する

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Generate.h"
#include "Grid.h"
#include "Piece.h"
#include "Solve.h"

namespace CubelithCoreTests
{
	// SolveTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace SolveTestDetail
	{
		/** 2x2x2 を 2 枚に分けたスラブ（1x2x2）のボクセル。TS の slabVoxels */
		TArray<Cubelith::FVec3> SlabVoxels()
		{
			return TArray<Cubelith::FVec3>{
				Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(0, 0, 1), Cubelith::Vec3(0, 1, 0), Cubelith::Vec3(0, 1, 1) };
		}

		/** 同じ形のスラブ 2 枚（id 0 / 1）。TS の slabs */
		TArray<Cubelith::FPiece> Slabs()
		{
			return TArray<Cubelith::FPiece>{
				Cubelith::CreatePiece(0, SlabVoxels()), Cubelith::CreatePiece(1, SlabVoxels()) };
		}

		/** 配置を組み立てる小さな入口（TS の place / slabPlacement に相当）。向きは恒等 */
		Cubelith::FPlacement Place(int32 PieceId, const Cubelith::FVec3& Position)
		{
			Cubelith::FPlacement Placement;
			Placement.PieceId = PieceId;
			Placement.Orientation = Cubelith::IdentityOrientation;
			Placement.Position = Position;
			return Placement;
		}

		/**
		 * スラブを x にずらして置く（TS の slabPlacement）。
		 * 局所原点が (0,0,0) に来るよう正規化されているので、置きたい絶対座標をそのまま渡せる
		 */
		Cubelith::FPlacement SlabPlacement(int32 Id, int32 X)
		{
			return Place(Id, Cubelith::Vec3(X, 0, 0));
		}

		/** 試す空間サイズ（TS の SIZES） */
		const TArray<int32>& Sizes()
		{
			static const TArray<int32> Values{ 3, 4, 5, 6, 7 };
			return Values;
		}

		/** 試すシード（TS の SEEDS。generate.test.ts とは別の並び） */
		const TArray<uint32>& Seeds()
		{
			static const TArray<uint32> Values{ 1, 7, 12345 };
			return Values;
		}

		/** 6 近傍のオフセット（1 マスずらす方向。TS の STEPS） */
		const TArray<Cubelith::FVec3>& Steps()
		{
			static const TArray<Cubelith::FVec3> Values{
				Cubelith::Vec3(1, 0, 0), Cubelith::Vec3(-1, 0, 0), Cubelith::Vec3(0, 1, 0),
				Cubelith::Vec3(0, -1, 0), Cubelith::Vec3(0, 0, 1), Cubelith::Vec3(0, 0, -1) };
			return Values;
		}

		/** M の代表値（下限・中間・上限。TS の representativePieceCounts） */
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

		/** 配置の 1 つだけを差し替えた配列を返す（TS の replaceAt） */
		TArray<Cubelith::FPlacement> ReplaceAt(
			TArrayView<const Cubelith::FPlacement> Placements, int32 Index, const Cubelith::FPlacement& Placement)
		{
			TArray<Cubelith::FPlacement> Copy(Placements.GetData(), Placements.Num());
			Copy[Index] = Placement;
			return Copy;
		}

		/** 全体を平行移動した配置（TS の translateAll） */
		TArray<Cubelith::FPlacement> TranslateAll(TArrayView<const Cubelith::FPlacement> Placements, const Cubelith::FVec3& Shift)
		{
			TArray<Cubelith::FPlacement> Result;
			Result.Reserve(Placements.Num());
			for (const Cubelith::FPlacement& Placement : Placements)
			{
				Cubelith::FPlacement Moved = Placement;
				Moved.Position = Cubelith::AddVec3(Placement.Position, Shift);
				Result.Add(Moved);
			}
			return Result;
		}

		/** 位置だけを変えた配置を返す（TS の { ...placement, position }） */
		Cubelith::FPlacement MoveTo(const Cubelith::FPlacement& Placement, const Cubelith::FVec3& Position)
		{
			Cubelith::FPlacement Moved = Placement;
			Moved.Position = Position;
			return Moved;
		}

		/** 向きだけを変えた配置を返す（TS の { ...placement, orientation }） */
		Cubelith::FPlacement Rotate(const Cubelith::FPlacement& Placement, int32 Orientation)
		{
			Cubelith::FPlacement Rotated = Placement;
			Rotated.Orientation = Orientation;
			return Rotated;
		}

		/** ボクセル集合を比較用の集合にする（TS の keySet。C++ では TSet<FVec3> をそのまま使える） */
		TSet<Cubelith::FVec3> KeySet(const TArray<Cubelith::FVec3>& Voxels)
		{
			return TSet<Cubelith::FVec3>(Voxels);
		}

		/** N / M / シードの組み合わせをメッセージに出す */
		FString Describe(int32 N, int32 M, uint32 Seed)
		{
			return FString::Printf(TEXT("N=%d / M=%d / seed=%u"), N, M, Seed);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSolveTwoSlabsSolvedTest, "CUBELITH.Core.Solve.TwoSlabsSolved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSolveTwoSlabsSolvedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 2 枚のスラブが並ぶとクリア
	const TArray<Cubelith::FPiece> Slabs = SolveTestDetail::Slabs();
	const TArray<Cubelith::FPlacement> Placements{
		SolveTestDetail::SlabPlacement(0, 0), SolveTestDetail::SlabPlacement(1, 1) };

	TestTrue(TEXT("2 枚のスラブが並ぶとクリア"), Cubelith::IsSolved(Slabs, Placements, 2));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSolveOriginDoesNotMatterTest, "CUBELITH.Core.Solve.OriginDoesNotMatter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSolveOriginDoesNotMatterTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 原点は問わない（どこにあってもクリア）
	const TArray<Cubelith::FPiece> Slabs = SolveTestDetail::Slabs();
	const TArray<Cubelith::FPlacement> Shifted{
		SolveTestDetail::Place(0, Cubelith::Vec3(-5, 3, -9)), SolveTestDetail::Place(1, Cubelith::Vec3(-4, 3, -9)) };

	TestTrue(TEXT("原点は問わない"), Cubelith::IsSolved(Slabs, Shifted, 2));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSolveSeparatedNotSolvedTest, "CUBELITH.Core.Solve.SeparatedNotSolved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSolveSeparatedNotSolvedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 離れているとクリアにならない（外接ボックスの x が 3 マスに広がる）
	const TArray<Cubelith::FPiece> Slabs = SolveTestDetail::Slabs();
	const TArray<Cubelith::FPlacement> Placements{
		SolveTestDetail::SlabPlacement(0, 0), SolveTestDetail::SlabPlacement(1, 2) };

	TestFalse(TEXT("離れているとクリアにならない"), Cubelith::IsSolved(Slabs, Placements, 2));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSolveOverlappingNotSolvedTest, "CUBELITH.Core.Solve.OverlappingNotSolved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSolveOverlappingNotSolvedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 重なっているとクリアにならない
	const TArray<Cubelith::FPiece> Slabs = SolveTestDetail::Slabs();
	const TArray<Cubelith::FPlacement> Placements{
		SolveTestDetail::SlabPlacement(0, 0), SolveTestDetail::SlabPlacement(1, 0) };

	TestFalse(TEXT("重なっているとクリアにならない"), Cubelith::IsSolved(Slabs, Placements, 2));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSolveVoxelCountMismatchNotSolvedTest, "CUBELITH.Core.Solve.VoxelCountMismatchNotSolved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSolveVoxelCountMismatchNotSolvedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// ボクセル総数が N³ でなければクリアにならない（2 枚で 8 個。N=3 なら 27 個必要）
	const TArray<Cubelith::FPiece> Slabs = SolveTestDetail::Slabs();
	const TArray<Cubelith::FPlacement> Placements{
		SolveTestDetail::SlabPlacement(0, 0), SolveTestDetail::SlabPlacement(1, 1) };

	TestFalse(TEXT("ボクセル総数が N³ でなければクリアにならない"), Cubelith::IsSolved(Slabs, Placements, 3));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSolveGeneratedSolutionTest, "CUBELITH.Core.Solve.GeneratedSolution",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSolveGeneratedSolutionTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 生成結果の解答配置でクリア、1 ピースを 1 マスずらすと偽
	int32 Checked = 0;
	for (const int32 N : SolveTestDetail::Sizes())
	{
		for (const int32 M : SolveTestDetail::RepresentativePieceCounts(N))
		{
			for (const uint32 Seed : SolveTestDetail::Seeds())
			{
				const FString What = SolveTestDetail::Describe(N, M, Seed);
				const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(N, M, Seed);
				++Checked;

				if (!Cubelith::IsSolved(Puzzle.Pieces, Puzzle.Solution, N))
				{
					AddError(FString::Printf(TEXT("%s: 解答配置がクリアにならない"), *What));
					continue;
				}

				// 全ピースを +x に 1 マスずらす（先頭のピースは 6 方向すべて試す）
				bool bFailed = false;
				for (int32 Index = 0; Index < Puzzle.Solution.Num() && !bFailed; ++Index)
				{
					const Cubelith::FPlacement& Placement = Puzzle.Solution[Index];
					const int32 StepNum = Index == 0 ? SolveTestDetail::Steps().Num() : 1;
					for (int32 StepIndex = 0; StepIndex < StepNum; ++StepIndex)
					{
						const Cubelith::FVec3& Step = SolveTestDetail::Steps()[StepIndex];
						const Cubelith::FPlacement Moved =
							SolveTestDetail::MoveTo(Placement, Cubelith::AddVec3(Placement.Position, Step));
						if (Cubelith::IsSolved(Puzzle.Pieces, SolveTestDetail::ReplaceAt(Puzzle.Solution, Index, Moved), N))
						{
							AddError(FString::Printf(TEXT("%s: ピース %d を (%d,%d,%d) ずらしてもクリアのままになった"),
								*What, Index, Step.X, Step.Y, Step.Z));
							bFailed = true;
							break;
						}
					}
				}
			}
		}
	}

	// 5 サイズ × M の代表値 3 通り × シード 3 個
	TestEqual(TEXT("確かめた組み合わせの数"), Checked, 45);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSolveTranslatedSolutionTest, "CUBELITH.Core.Solve.TranslatedSolution",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSolveTranslatedSolutionTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 立方体全体を平行移動してもクリアのまま
	for (const int32 N : SolveTestDetail::Sizes())
	{
		const TArray<Cubelith::FVec3> Shifts{
			Cubelith::Vec3(1, 0, 0), Cubelith::Vec3(-9, 4, 13), Cubelith::Vec3(-N, -N, -N) };
		for (const Cubelith::FVec3& Shift : Shifts)
		{
			const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(N, Cubelith::MaxPieces(N), 5);
			TestTrue(FString::Printf(TEXT("N=%d を (%d,%d,%d) 平行移動してもクリア"), N, Shift.X, Shift.Y, Shift.Z),
				Cubelith::IsSolved(Puzzle.Pieces, SolveTestDetail::TranslateAll(Puzzle.Solution, Shift), N));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSolveRotatedPieceNotSolvedTest, "CUBELITH.Core.Solve.RotatedPieceNotSolved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSolveRotatedPieceNotSolvedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 1 ピースを回すと偽（回して形が変わる場合）
	for (const int32 N : SolveTestDetail::Sizes())
	{
		for (const int32 M : SolveTestDetail::RepresentativePieceCounts(N))
		{
			const FString What = SolveTestDetail::Describe(N, M, 20260904);
			const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(N, M, 20260904);

			int32 Checked = 0;
			for (int32 Index = 0; Index < Puzzle.Pieces.Num(); ++Index)
			{
				const Cubelith::FPiece& Piece = Puzzle.Pieces[Index];
				const Cubelith::FPlacement& Placement = Puzzle.Solution[Index];
				if (Piece.Voxels.Num() < 2)
				{
					continue;
				}

				const TSet<Cubelith::FVec3> Original = SolveTestDetail::KeySet(Cubelith::PlacedVoxels(Piece, Placement));
				for (int32 Orientation = 1; Orientation < Cubelith::OrientationCount; ++Orientation)
				{
					const Cubelith::FPlacement Rotated = SolveTestDetail::Rotate(Placement, Orientation);
					const TSet<Cubelith::FVec3> Voxels = SolveTestDetail::KeySet(Cubelith::PlacedVoxels(Piece, Rotated));
					// 回転で形が変わらない向き（対称なピース）は判定が変わらないので除く
					if (Voxels.Num() == Original.Num() && Voxels.Includes(Original))
					{
						continue;
					}

					if (Cubelith::IsSolved(Puzzle.Pieces, SolveTestDetail::ReplaceAt(Puzzle.Solution, Index, Rotated), N))
					{
						AddError(FString::Printf(TEXT("%s: ピース %d を向き %d に回してもクリアのままになった"),
							*What, Index, Orientation));
					}
					++Checked;
					break;
				}
			}

			// どの N / M でも「回すと形が変わるピース」が最低 1 つはある
			TestTrue(FString::Printf(TEXT("%s: 回すと形が変わるピースがある"), *What), Checked > 0);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSolveSwappedPiecesNotSolvedTest, "CUBELITH.Core.Solve.SwappedPiecesNotSolved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSolveSwappedPiecesNotSolvedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 2 つのピースを入れ替えると偽（形が違う場合）
	const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(4, 8, 314);
	const Cubelith::FPlacement& A = Puzzle.Solution[0];
	const Cubelith::FPlacement& B = Puzzle.Solution[1];

	const TArray<Cubelith::FPlacement> Swapped = SolveTestDetail::ReplaceAt(
		SolveTestDetail::ReplaceAt(Puzzle.Solution, 0, SolveTestDetail::MoveTo(A, B.Position)),
		1, SolveTestDetail::MoveTo(B, A.Position));

	TestFalse(TEXT("2 つのピースを入れ替えるとクリアにならない"), Cubelith::IsSolved(Puzzle.Pieces, Swapped, 4));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
