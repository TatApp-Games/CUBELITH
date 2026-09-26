// WebMock/tests/hint.test.ts の pickHintPiece の正常系 10 件の移植（RULES.md 3.7）
// 移植しないテスト: 「solution に無い id が placements にあれば Error」（checkf で停止するため）
// TS の lockedIds は Iterable<number>（配列と Set の両方を渡していた）。C++ は TArrayView<const int32> 1 本なので、
// 配列で渡す形に揃えた（PickHintPiece は「含むかどうか」しか見ないので結果は変わらない）

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Generate.h"
#include "Grid.h"
#include "Hint.h"
#include "Piece.h"

namespace CubelithCoreTests
{
	// HintTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace HintTestDetail
	{
		/** 配置を組み立てる小さな入口 */
		Cubelith::FPlacement Place(int32 PieceId, int32 Orientation, const Cubelith::FVec3& Position)
		{
			Cubelith::FPlacement Placement;
			Placement.PieceId = PieceId;
			Placement.Orientation = Orientation;
			Placement.Position = Position;
			return Placement;
		}

		/** id 0..Count-1 の解答配置。位置と向きは id ごとに変える（TS の solutionOf） */
		TArray<Cubelith::FPlacement> SolutionOf(int32 Count)
		{
			TArray<Cubelith::FPlacement> Solution;
			Solution.Reserve(Count);
			for (int32 Id = 0; Id < Count; ++Id)
			{
				Solution.Add(Place(Id, Id % Cubelith::OrientationCount, Cubelith::Vec3(Id, 0, 0)));
			}
			return Solution;
		}

		/** 解答から 1 マスずらした配置（位置違い。TS の movedFrom） */
		Cubelith::FPlacement MovedFrom(const Cubelith::FPlacement& Placement)
		{
			return Place(Placement.PieceId, Placement.Orientation,
				Cubelith::Vec3(Placement.Position.X, Placement.Position.Y + 5, Placement.Position.Z));
		}

		/** 解答と向きだけ違う配置（TS の turnedFrom） */
		Cubelith::FPlacement TurnedFrom(const Cubelith::FPlacement& Placement)
		{
			return Place(Placement.PieceId, (Placement.Orientation + 1) % Cubelith::OrientationCount, Placement.Position);
		}

		/** 解答の全ピースを MovedFrom でずらした配置（TS の solution.map(movedFrom)） */
		TArray<Cubelith::FPlacement> AllMovedFrom(TArrayView<const Cubelith::FPlacement> Solution)
		{
			TArray<Cubelith::FPlacement> Placements;
			Placements.Reserve(Solution.Num());
			for (const Cubelith::FPlacement& Placement : Solution)
			{
				Placements.Add(MovedFrom(Placement));
			}
			return Placements;
		}

		/** 配列から id で配置を引く（TS の pick）。無ければ nullptr */
		const Cubelith::FPlacement* Find(TArrayView<const Cubelith::FPlacement> Placements, int32 PieceId)
		{
			return Placements.FindByPredicate(
				[PieceId](const Cubelith::FPlacement& Placement) -> bool { return Placement.PieceId == PieceId; });
		}

		/** TOptional<int32> が期待どおりかを見る。未設定（TS の null）も期待値にできる */
		void CheckPicked(FAutomationTestBase& Test, const TCHAR* What, const TOptional<int32>& Actual, const TOptional<int32>& Expected)
		{
			if (Actual.IsSet() != Expected.IsSet())
			{
				Test.AddError(FString::Printf(TEXT("%s: 期待 %s / 実際 %s"), What,
					Expected.IsSet() ? *FString::Printf(TEXT("id %d"), Expected.GetValue()) : TEXT("未設定"),
					Actual.IsSet() ? *FString::Printf(TEXT("id %d"), Actual.GetValue()) : TEXT("未設定")));
				return;
			}
			if (Actual.IsSet())
			{
				Test.TestEqual(What, Actual.GetValue(), Expected.GetValue());
			}
		}

		/** 未設定（TS の null）を期待する */
		void CheckNone(FAutomationTestBase& Test, const TCHAR* What, const TOptional<int32>& Actual)
		{
			CheckPicked(Test, What, Actual, TOptional<int32>());
		}

		/** 特定の id を期待する */
		void CheckId(FAutomationTestBase& Test, const TCHAR* What, const TOptional<int32>& Actual, int32 Expected)
		{
			CheckPicked(Test, What, Actual, TOptional<int32>(Expected));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHintNoUnlockedTest, "CUBELITH.Core.Hint.NoUnlocked",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「未固定が 0 個なら null」
bool FCubelithHintNoUnlockedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::HintTestDetail;

	const TArray<Cubelith::FPlacement> Solution = SolutionOf(4);
	const TArray<Cubelith::FPlacement> Placements = AllMovedFrom(Solution);
	const TArray<int32> Locked{ 0, 1, 2, 3 };

	CheckNone(*this, TEXT("未固定が 0 個"), Cubelith::PickHintPiece(Placements, Solution, Locked));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHintOneUnlockedTest, "CUBELITH.Core.Hint.OneUnlocked",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「未固定が 1 個なら null（最後の 1 ピースはヒントを使えない）」
bool FCubelithHintOneUnlockedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::HintTestDetail;

	const TArray<Cubelith::FPlacement> Solution = SolutionOf(4);
	const TArray<Cubelith::FPlacement> Placements = AllMovedFrom(Solution);

	const TArray<int32> LockedLow{ 0, 1, 2 };
	CheckNone(*this, TEXT("未固定が id 3 だけ"), Cubelith::PickHintPiece(Placements, Solution, LockedLow));

	// TS はここで Set を渡していた。C++ は並びを問わない配列で同じことを見る
	const TArray<int32> LockedHigh{ 1, 2, 3 };
	CheckNone(*this, TEXT("未固定が id 0 だけ"), Cubelith::PickHintPiece(Placements, Solution, LockedHigh));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHintPicksSmallestMismatchedTest, "CUBELITH.Core.Hint.PicksSmallestMismatched",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「未固定が 2 個以上なら、解答とずれているうち id 最小を返す」
bool FCubelithHintPicksSmallestMismatchedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::HintTestDetail;

	const TArray<Cubelith::FPlacement> Solution = SolutionOf(5);
	const TArray<Cubelith::FPlacement> Placements = AllMovedFrom(Solution);

	CheckId(*this, TEXT("固定なし"), Cubelith::PickHintPiece(Placements, Solution, TArray<int32>{}), 0);
	CheckId(*this, TEXT("0 を固定"), Cubelith::PickHintPiece(Placements, Solution, TArray<int32>{ 0 }), 1);
	CheckId(*this, TEXT("0 と 2 を固定"), Cubelith::PickHintPiece(Placements, Solution, TArray<int32>{ 0, 2 }), 1);
	CheckId(*this, TEXT("0 と 1 を固定"), Cubelith::PickHintPiece(Placements, Solution, TArray<int32>{ 0, 1 }), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHintSkipsMatchingTest, "CUBELITH.Core.Hint.SkipsMatching",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「解答と一致しているピースは id が小さくても選ばれない」
bool FCubelithHintSkipsMatchingTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::HintTestDetail;

	const TArray<Cubelith::FPlacement> Solution = SolutionOf(5);

	// 0 と 1 は解答どおり、2 以降はずれている
	TArray<Cubelith::FPlacement> Placements;
	Placements.Reserve(Solution.Num());
	for (const Cubelith::FPlacement& Answer : Solution)
	{
		Placements.Add(Answer.PieceId <= 1 ? Answer : MovedFrom(Answer));
	}

	CheckId(*this, TEXT("ずれている中の id 最小"), Cubelith::PickHintPiece(Placements, Solution, TArray<int32>{}), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHintOrientationOnlyMismatchTest, "CUBELITH.Core.Hint.OrientationOnlyMismatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「向きだけ違うピースも「ずれている」と見なす」
bool FCubelithHintOrientationOnlyMismatchTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::HintTestDetail;

	const TArray<Cubelith::FPlacement> Solution = SolutionOf(4);

	TArray<Cubelith::FPlacement> Placements;
	Placements.Reserve(Solution.Num());
	for (const Cubelith::FPlacement& Answer : Solution)
	{
		Placements.Add(Answer.PieceId == 2 ? TurnedFrom(Answer) : Answer);
	}

	CheckId(*this, TEXT("向きだけ違う id 2"), Cubelith::PickHintPiece(Placements, Solution, TArray<int32>{}), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHintAllMatchingTest, "CUBELITH.Core.Hint.AllMatching",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「未固定がすべて解答と一致していれば、未固定の id 最小を返す」
bool FCubelithHintAllMatchingTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::HintTestDetail;

	const TArray<Cubelith::FPlacement> Solution = SolutionOf(4);
	const TArray<Cubelith::FPlacement> Placements = Solution;

	CheckId(*this, TEXT("固定なし"), Cubelith::PickHintPiece(Placements, Solution, TArray<int32>{}), 0);
	CheckId(*this, TEXT("0 を固定"), Cubelith::PickHintPiece(Placements, Solution, TArray<int32>{ 0 }), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHintOrderIndependentTest, "CUBELITH.Core.Hint.OrderIndependent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「placements の並びが変わっても結果は同じ」
bool FCubelithHintOrderIndependentTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::HintTestDetail;

	const TArray<Cubelith::FPlacement> Solution = SolutionOf(5);

	TArray<Cubelith::FPlacement> Placements;
	Placements.Reserve(Solution.Num());
	for (const Cubelith::FPlacement& Answer : Solution)
	{
		Placements.Add(Answer.PieceId <= 1 ? Answer : MovedFrom(Answer));
	}

	TArray<Cubelith::FPlacement> Shuffled = Placements;
	Algo::Reverse(Shuffled);

	CheckId(*this, TEXT("逆順でも同じ"), Cubelith::PickHintPiece(Shuffled, Solution, TArray<int32>{}), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHintDeterministicTest, "CUBELITH.Core.Hint.Deterministic",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「同じ入力なら常に同じ結果」
bool FCubelithHintDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::HintTestDetail;

	const TArray<Cubelith::FPlacement> Solution = SolutionOf(6);

	TArray<Cubelith::FPlacement> Placements;
	Placements.Reserve(Solution.Num());
	for (const Cubelith::FPlacement& Answer : Solution)
	{
		Placements.Add(Answer.PieceId % 2 == 0 ? Answer : MovedFrom(Answer));
	}

	const TArray<int32> Locked{ 0, 1 };
	const TOptional<int32> First = Cubelith::PickHintPiece(Placements, Solution, Locked);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		CheckPicked(*this, TEXT("何度呼んでも同じ"), Cubelith::PickHintPiece(Placements, Solution, Locked), First);
	}
	CheckId(*this, TEXT("最初の結果"), First, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHintLockUntilNoneTest, "CUBELITH.Core.Hint.LockUntilNone",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「固定を進めていくと最後は null になる」
bool FCubelithHintLockUntilNoneTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::HintTestDetail;

	const TArray<Cubelith::FPlacement> Solution = SolutionOf(4);
	const TArray<Cubelith::FPlacement> Placements = AllMovedFrom(Solution);

	TArray<int32> Locked;
	TArray<int32> Picked;
	for (;;)
	{
		const TOptional<int32> Next = Cubelith::PickHintPiece(Placements, Solution, Locked);
		if (!Next.IsSet())
		{
			break;
		}
		Picked.Add(Next.GetValue());
		Locked.Add(Next.GetValue());
		// 万一止まらない実装になっていてもテストが固まらないようにする（TS の for(;;) には無い保険）
		if (Picked.Num() > Solution.Num())
		{
			break;
		}
	}

	// 4 ピースなら 3 回まで（最後の 1 ピースは残る）
	const TArray<int32> Expected{ 0, 1, 2 };
	TestEqual(TEXT("選ばれた回数"), Picked.Num(), Expected.Num());
	if (Picked.Num() == Expected.Num())
	{
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("%d 回目に選ばれた id"), Index), Picked[Index], Expected[Index]);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHintGeneratedPuzzleTest, "CUBELITH.Core.Hint.GeneratedPuzzle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「生成した実際のパズルでも動く」
bool FCubelithHintGeneratedPuzzleTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::HintTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Cubelith::GeneratePuzzle(4, 5, 20260906);
	const TArray<Cubelith::FPlacement> Scattered = Cubelith::ScatterPlacements(Generated.Pieces, 4, 20260906);

	// 散らした直後はすべてずれている想定だが、たまたま一致していても id 最小が返るのは同じ
	const TOptional<int32> First = Cubelith::PickHintPiece(Scattered, Generated.Solution, TArray<int32>{});
	CheckId(*this, TEXT("散らし直後の 1 回目"), First, 0);

	// ヒントを適用した状態（解答位置に置いて固定）にしても、次は未固定の中から選ばれる
	const Cubelith::FPlacement* Answer = Find(Generated.Solution, 0);
	if (Answer == nullptr)
	{
		AddError(TEXT("テスト: ピース 0 の解答配置が無い"));
		return false;
	}

	TArray<Cubelith::FPlacement> Applied;
	Applied.Reserve(Scattered.Num());
	for (const Cubelith::FPlacement& Placement : Scattered)
	{
		Applied.Add(Placement.PieceId == 0 ? *Answer : Placement);
	}

	CheckId(*this, TEXT("id 0 を固定した後"), Cubelith::PickHintPiece(Applied, Generated.Solution, TArray<int32>{ 0 }), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
