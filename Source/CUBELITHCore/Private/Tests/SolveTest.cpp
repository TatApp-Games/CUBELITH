// WebMock/tests/solve.test.ts の isSolved（手作りの小さな例）の正常系の移植
// 移植しないテスト: 「不正な N は RangeError」（checkf で停止するため）
// 移植しないテスト: 「ピースと配置の対応が壊れていたら Error」（checkf で停止するため）
// 005 で足すテスト: isSolved（生成結果の解答配置）の「解答配置でクリア、1 ピースを 1 マスずらすと偽」（generatePuzzle が要る）
// 005 で足すテスト: isSolved（生成結果の解答配置）の「立方体全体を平行移動してもクリアのまま」（generatePuzzle が要る）
// 005 で足すテスト: isSolved（生成結果の解答配置）の「1 ピースを回すと偽（回して形が変わる場合）」（generatePuzzle が要る）
// 005 で足すテスト: isSolved（生成結果の解答配置）の「2 つのピースを入れ替えると偽（形が違う場合）」（generatePuzzle が要る）

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

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

#endif // WITH_DEV_AUTOMATION_TESTS
