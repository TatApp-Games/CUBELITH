// CubelithProgress（残りピース数の数え上げ。RULES.md 6 章）のテスト。移植元 WebMock/src/ui/progress.ts に
// テストは無いので、CubelithProgress.h の「解釈:」に書いた手順のとおりに数えられているかを確かめる。
// 見るのは 4 つ: 解答配置なら 0 / 散らした直後は全ピースが未確定 / ピース 1 個だけの塊は本体にしない /
// 本体の外接ボックスが N を超えるときは全部未確定。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithProgress.h"
#include "Generate.h"
#include "Grid.h"
#include "Piece.h"

namespace CubelithRenderTests
{
	// ProgressTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace ProgressTestDetail
	{
		/** 1 ボクセルだけのピース（塊の作り方を手で決めたいときに使う） */
		Cubelith::FPiece UnitPiece(int32 Id)
		{
			const Cubelith::FVec3 Voxels[1] = { Cubelith::Vec3(0, 0, 0) };
			return Cubelith::CreatePiece(Id, Voxels);
		}

		/** 配置を組み立てる小さな入口（GameTest.cpp の Place と同じ） */
		Cubelith::FPlacement Place(int32 PieceId, const Cubelith::FVec3& Position)
		{
			Cubelith::FPlacement Placement;
			Placement.PieceId = PieceId;
			Placement.Orientation = Cubelith::IdentityOrientation;
			Placement.Position = Position;
			return Placement;
		}

		/** 確かめに使うシード（散らしと生成で同じものを使う） */
		const uint32 Seeds[4] = { 1u, 7u, 20260904u, 4294967295u };
	}
}

// 1. 解答配置なら残り 0（クリアした瞬間は全ピースが 1 つの塊で外接ボックスが N×N×N）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithProgressSolutionIsZeroTest, "CUBELITH.Render.Progress.SolutionIsZero",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithProgressSolutionIsZeroTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::ProgressTestDetail;

	struct FCase
	{
		int32 N;
		int32 M;
	};
	const FCase Cases[3] = { { 3, 4 }, { 4, 6 }, { 5, 11 } };

	for (const FCase& Case : Cases)
	{
		for (const uint32 Seed : Seeds)
		{
			const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(Case.N, Case.M, Seed);
			const int32 Actual = Cubelith::UnsettledPieceCount(Puzzle.Pieces, Puzzle.Solution, Case.N);
			if (Actual != 0)
			{
				AddError(FString::Printf(TEXT("N=%d M=%d Seed=%u の解答配置で残り %d（期待 0）"),
					Case.N, Case.M, Seed, Actual));
			}
		}
	}

	return true;
}

// 2. 散らした直後は全ピースが未確定（1 個だけの塊を本体にしないので、離れて散っていれば全部残る）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithProgressScatterIsAllTest, "CUBELITH.Render.Progress.ScatterIsAll",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithProgressScatterIsAllTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::ProgressTestDetail;

	constexpr int32 N = 3;
	constexpr int32 M = 4;

	for (const uint32 Seed : Seeds)
	{
		const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(N, M, Seed);
		const TArray<Cubelith::FPlacement> Scattered = Cubelith::ScatterPlacements(Puzzle.Pieces, N, Seed);
		const int32 Actual = Cubelith::UnsettledPieceCount(Puzzle.Pieces, Scattered, N);
		if (Actual != M)
		{
			AddError(FString::Printf(TEXT("Seed=%u で散らした直後の残りが %d（期待 %d）"), Seed, Actual, M));
		}
	}

	return true;
}

// 3. 解答配置から 1 個だけ遠くへ動かすと、その 1 個だけが未確定になる
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithProgressMovedPieceIsUnsettledTest,
	"CUBELITH.Render.Progress.MovedPieceIsUnsettled",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithProgressMovedPieceIsUnsettledTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::ProgressTestDetail;

	constexpr int32 N = 3;
	constexpr int32 M = 4;
	constexpr uint32 Seed = 20260904u;

	const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(N, M, Seed);
	TArray<Cubelith::FPlacement> Placements = Puzzle.Solution;
	Placements[0].Position = Cubelith::AddVec3(Placements[0].Position, Cubelith::Vec3(20, 0, 0));

	const int32 Actual = Cubelith::UnsettledPieceCount(Puzzle.Pieces, Placements, N);
	if (Actual != 1)
	{
		AddError(FString::Printf(TEXT("1 個だけ離したときの残りが %d（期待 1）"), Actual));
	}

	// Pieces に無い id の配置は無視する（数にも入らない）
	Placements.Add(Place(999, Cubelith::Vec3(50, 0, 0)));
	const int32 WithUnknown = Cubelith::UnsettledPieceCount(Puzzle.Pieces, Placements, N);
	if (WithUnknown != 1)
	{
		AddError(FString::Printf(TEXT("未知の id を混ぜたときの残りが %d（期待 1。無視されるはず）"), WithUnknown));
	}

	return true;
}

// 4. ピース 1 個だけの塊は本体にしない（2 個以上の塊なら本体になる）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithProgressSinglePieceClumpIsNotBodyTest,
	"CUBELITH.Render.Progress.SinglePieceClumpIsNotBody",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithProgressSinglePieceClumpIsNotBodyTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::ProgressTestDetail;

	constexpr int32 N = 3;

	const Cubelith::FPiece Pieces[3] = { UnitPiece(0), UnitPiece(1), UnitPiece(2) };

	// どれも面で接していない → 最大の塊が 1 個なので本体にならず、全部未確定
	{
		const Cubelith::FPlacement Placements[3] = {
			Place(0, Cubelith::Vec3(0, 0, 0)),
			Place(1, Cubelith::Vec3(10, 0, 0)),
			Place(2, Cubelith::Vec3(20, 0, 0)),
		};
		const int32 Actual = Cubelith::UnsettledPieceCount(Pieces, Placements, N);
		if (Actual != 3)
		{
			AddError(FString::Printf(TEXT("離れた 3 個の残りが %d（期待 3）"), Actual));
		}
	}

	// 2 個が面で接し、残り 1 個が離れている → 接している 2 個が本体（外接ボックス 2×1×1 は N に収まる）
	{
		const Cubelith::FPlacement Placements[3] = {
			Place(0, Cubelith::Vec3(0, 0, 0)),
			Place(1, Cubelith::Vec3(1, 0, 0)),
			Place(2, Cubelith::Vec3(20, 0, 0)),
		};
		const int32 Actual = Cubelith::UnsettledPieceCount(Pieces, Placements, N);
		if (Actual != 1)
		{
			AddError(FString::Printf(TEXT("2 個が接しているときの残りが %d（期待 1）"), Actual));
		}
	}

	return true;
}

// 5. 本体の外接ボックスが N を超えるときは全部未確定（立方体になり得ないため）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithProgressBodyLargerThanNTest, "CUBELITH.Render.Progress.BodyLargerThanN",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithProgressBodyLargerThanNTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::ProgressTestDetail;

	constexpr int32 N = 3;

	// x 方向に 4 個つながった塊。外接ボックスは 4×1×1 で N=3 に収まらない
	const Cubelith::FPiece Pieces[4] = { UnitPiece(0), UnitPiece(1), UnitPiece(2), UnitPiece(3) };
	const Cubelith::FPlacement Placements[4] = {
		Place(0, Cubelith::Vec3(0, 0, 0)),
		Place(1, Cubelith::Vec3(1, 0, 0)),
		Place(2, Cubelith::Vec3(2, 0, 0)),
		Place(3, Cubelith::Vec3(3, 0, 0)),
	};

	const int32 Actual = Cubelith::UnsettledPieceCount(Pieces, Placements, N);
	if (Actual != 4)
	{
		AddError(FString::Printf(TEXT("N を超える塊の残りが %d（期待 4）"), Actual));
	}

	// 3 個（外接ボックス 3×1×1）なら収まるので確定する。境界の確かめ
	const Cubelith::FPlacement Fitting[3] = {
		Place(0, Cubelith::Vec3(0, 0, 0)),
		Place(1, Cubelith::Vec3(1, 0, 0)),
		Place(2, Cubelith::Vec3(2, 0, 0)),
	};
	const int32 FittingActual = Cubelith::UnsettledPieceCount(
		TArrayView<const Cubelith::FPiece>(Pieces, 3), Fitting, N);
	if (FittingActual != 0)
	{
		AddError(FString::Printf(TEXT("N に収まる塊の残りが %d（期待 0）"), FittingActual));
	}

	return true;
}

#endif
