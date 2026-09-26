// WebMock/tests/game.test.ts の正常系の移植（RULES.md 3.3 / 3.4）
// 移植しないテスト: 「replacePlacement の未知の id は例外」（checkf で停止するため）
// 移植しないテスト: 「未知の id の操作と、数の合わない reset は例外」（checkf で停止するため）
// 移植しないテスト: 「未知のピース id は例外」（checkf で停止するため。同テストの正常系だった
//                  「未知の id の lockKindOf は null」は LockUnlockDoesNotNotify に入れてある）
// 移植しないテスト: 「place の向き id が範囲外なら RangeError」（checkf で停止するため）
// TS の vi.fn() は無いので、呼ばれた回数とそのときの判定を FChangeLog に記録するリスナで代える

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Game.h"
#include "Generate.h"
#include "Grid.h"
#include "Piece.h"
#include "Solve.h"

namespace CubelithCoreTests
{
	// GameTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace GameTestDetail
	{
		/** テストで使うパズル（TS の N / M / SEED と同じ） */
		constexpr int32 N = 3;
		constexpr int32 M = 4;
		constexpr uint32 Seed = 20260904;

		const Cubelith::EAxis Axes[3] = { Cubelith::EAxis::X, Cubelith::EAxis::Y, Cubelith::EAxis::Z };
		const TCHAR* const AxisNames[3] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
		const int32 Directions[2] = { 1, -1 };

		/** テスト用のパズル一式（TS の puzzle()） */
		Cubelith::FGeneratedPuzzle Puzzle()
		{
			return Cubelith::GeneratePuzzle(N, M, Seed);
		}

		/** 配置を組み立てる小さな入口 */
		Cubelith::FPlacement Place(int32 PieceId, int32 Orientation, const Cubelith::FVec3& Position)
		{
			Cubelith::FPlacement Placement;
			Placement.PieceId = PieceId;
			Placement.Orientation = Orientation;
			Placement.Position = Position;
			return Placement;
		}

		/** 配列から id で配置を引く（TS の placementOf）。無ければ nullptr */
		const Cubelith::FPlacement* Find(TArrayView<const Cubelith::FPlacement> Placements, int32 PieceId)
		{
			return Placements.FindByPredicate(
				[PieceId](const Cubelith::FPlacement& Placement) -> bool { return Placement.PieceId == PieceId; });
		}

		/** 座標の 1 成分（軸ごとの差を見るのに使う） */
		int32 Component(const Cubelith::FVec3& V, Cubelith::EAxis Axis)
		{
			if (Axis == Cubelith::EAxis::X)
			{
				return V.X;
			}
			if (Axis == Cubelith::EAxis::Y)
			{
				return V.Y;
			}
			return V.Z;
		}

		/** 1 マス分の移動ベクトル（TS の unitStep）。Sign は +1 / -1 */
		Cubelith::FVec3 UnitStep(Cubelith::EAxis Axis, int32 Sign)
		{
			if (Axis == Cubelith::EAxis::X)
			{
				return Cubelith::Vec3(Sign, 0, 0);
			}
			if (Axis == Cubelith::EAxis::Y)
			{
				return Cubelith::Vec3(0, Sign, 0);
			}
			return Cubelith::Vec3(0, 0, Sign);
		}

		/** 期待する座標と一致するか。成分を直に比べるので EqualsVec3 の実装には依存しない */
		bool CheckVec3(FAutomationTestBase& Test, const FString& What, const Cubelith::FVec3& Actual, const Cubelith::FVec3& Expected)
		{
			if (Actual.X == Expected.X && Actual.Y == Expected.Y && Actual.Z == Expected.Z)
			{
				return true;
			}
			Test.AddError(FString::Printf(TEXT("%s: 期待 (%d,%d,%d) / 実際 (%d,%d,%d)"),
				*What, Expected.X, Expected.Y, Expected.Z, Actual.X, Actual.Y, Actual.Z));
			return false;
		}

		/** 固定の種類をメッセージに出す名前 */
		FString LockKindName(const TOptional<Cubelith::ELockKind>& Kind)
		{
			if (!Kind.IsSet())
			{
				return TEXT("未設定");
			}
			return Kind.GetValue() == Cubelith::ELockKind::Manual ? TEXT("Manual") : TEXT("Hint");
		}

		/** 固定の種類が期待どおりか（未設定は TS の null） */
		bool CheckLockKind(FAutomationTestBase& Test, const FString& What,
			const TOptional<Cubelith::ELockKind>& Actual, const TOptional<Cubelith::ELockKind>& Expected)
		{
			if (Actual.IsSet() == Expected.IsSet() && (!Actual.IsSet() || Actual.GetValue() == Expected.GetValue()))
			{
				return true;
			}
			Test.AddError(FString::Printf(TEXT("%s: 期待 %s / 実際 %s"), *What, *LockKindName(Expected), *LockKindName(Actual)));
			return false;
		}

		/** 固定していないことを期待する */
		bool CheckUnlocked(FAutomationTestBase& Test, const FString& What, const TOptional<Cubelith::ELockKind>& Actual)
		{
			return CheckLockKind(Test, What, Actual, TOptional<Cubelith::ELockKind>());
		}

		/** id の配列が期待どおりか（LockedIds() は昇順なので並びごと比べる） */
		bool CheckIds(FAutomationTestBase& Test, const FString& What, const TArray<int32>& Actual, const TArray<int32>& Expected)
		{
			bool bSame = Actual.Num() == Expected.Num();
			for (int32 Index = 0; bSame && Index < Expected.Num(); ++Index)
			{
				bSame = Actual[Index] == Expected[Index];
			}
			if (bSame)
			{
				return true;
			}

			auto Join = [](const TArray<int32>& Ids) -> FString
			{
				FString Text;
				for (int32 Index = 0; Index < Ids.Num(); ++Index)
				{
					Text += (Index > 0 ? TEXT(",") : TEXT("")) + FString::FromInt(Ids[Index]);
				}
				return Text.IsEmpty() ? TEXT("(空)") : Text;
			};
			Test.AddError(FString::Printf(TEXT("%s: 期待 [%s] / 実際 [%s]"), *What, *Join(Expected), *Join(Actual)));
			return false;
		}

		/** 2 つの配置が完全に一致するか（id・向き・位置） */
		bool CheckSamePlacement(FAutomationTestBase& Test, const FString& What,
			const Cubelith::FPlacement& Actual, const Cubelith::FPlacement& Expected)
		{
			if (Actual == Expected)
			{
				return true;
			}
			Test.AddError(FString::Printf(
				TEXT("%s: 期待 {id %d, 向き %d, (%d,%d,%d)} / 実際 {id %d, 向き %d, (%d,%d,%d)}"), *What,
				Expected.PieceId, Expected.Orientation, Expected.Position.X, Expected.Position.Y, Expected.Position.Z,
				Actual.PieceId, Actual.Orientation, Actual.Position.X, Actual.Position.Y, Actual.Position.Z));
			return false;
		}

		/** ピース id の現在の配置を写して返す。無ければ AddError して false */
		bool CopyPlacement(FAutomationTestBase& Test, const Cubelith::FGame& Game, int32 PieceId, Cubelith::FPlacement& OutPlacement)
		{
			const Cubelith::FPlacement* Placement = Game.PlacementOf(PieceId);
			if (Placement == nullptr)
			{
				Test.AddError(FString::Printf(TEXT("テスト: ピース %d の配置が無い"), PieceId));
				return false;
			}
			// Placements() の中身は更新のたびに作り直されるので、必ず写しを持つ
			OutPlacement = *Placement;
			return true;
		}

		/** UI の回転ボタン 6 個（軸 3 × 向き 2）の 1 手 */
		struct FRotationStep
		{
			Cubelith::EAxis Axis = Cubelith::EAxis::X;
			int32 Dir = 1;
		};

		/** TS の steps と同じ並び */
		const TArray<FRotationStep>& RotationSteps()
		{
			static const TArray<FRotationStep> Steps{
				{ Cubelith::EAxis::X, 1 }, { Cubelith::EAxis::X, -1 },
				{ Cubelith::EAxis::Y, 1 }, { Cubelith::EAxis::Y, -1 },
				{ Cubelith::EAxis::Z, 1 }, { Cubelith::EAxis::Z, -1 } };
			return Steps;
		}

		/**
		 * UI の回転ボタン 6 個だけを使って From → To へ辿る手順を幅優先で求める（TS の rotationPath）。
		 * 6 手で 24 通りすべてに届くことは GridTest の RotateOrientationReachesAll が見ている。
		 * 辿れなければ空を返して bOutFound を false にする（テスト側で AddError する）。
		 */
		TArray<FRotationStep> RotationPath(int32 From, int32 To, bool& bOutFound)
		{
			TArray<FRotationStep> Path;
			bOutFound = true;
			if (From == To)
			{
				return Path;
			}

			TArray<int32> PreviousOrientation;
			PreviousOrientation.Init(INDEX_NONE, Cubelith::OrientationCount);
			TArray<FRotationStep> PreviousStep;
			PreviousStep.SetNum(Cubelith::OrientationCount);
			TArray<bool> Seen;
			Seen.Init(false, Cubelith::OrientationCount);
			Seen[From] = true;

			TArray<int32> Queue;
			Queue.Add(From);
			int32 Head = 0;
			bool bReached = false;
			while (Head < Queue.Num() && !bReached)
			{
				const int32 Current = Queue[Head++];
				for (const FRotationStep& Step : RotationSteps())
				{
					const int32 Next = Cubelith::RotateOrientation(Current, Step.Axis, Step.Dir);
					if (Seen[Next])
					{
						continue;
					}
					Seen[Next] = true;
					PreviousOrientation[Next] = Current;
					PreviousStep[Next] = Step;
					if (Next == To)
					{
						bReached = true;
						break;
					}
					Queue.Add(Next);
				}
			}

			if (!bReached)
			{
				bOutFound = false;
				return Path;
			}

			for (int32 Node = To; Node != From; Node = PreviousOrientation[Node])
			{
				Path.Insert(PreviousStep[Node], 0);
			}
			return Path;
		}

		/** 配置が変わった回数と、そのときのクリア判定・配置の記録（TS の vi.fn() の代わり） */
		struct FChangeLog
		{
			int32 Count = 0;
			TArray<bool> Solved;
			TArray<Cubelith::FPlacement> LastPlacements;

			void Clear()
			{
				Count = 0;
				Solved.Reset();
				LastPlacements.Reset();
			}
		};

		/** FChangeLog に書き込むリスナ。Log はゲームより長生きすること */
		Cubelith::FGameChangeListener MakeListener(FChangeLog& Log)
		{
			return [&Log](TArrayView<const Cubelith::FPlacement> Placements, bool bSolved) -> void
			{
				++Log.Count;
				Log.Solved.Add(bSolved);
				Log.LastPlacements = TArray<Cubelith::FPlacement>(Placements.GetData(), Placements.Num());
			};
		}
	}
}

// ---- movePlacement ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameMovePlacementAddsDeltaTest, "CUBELITH.Core.Game.MovePlacementAddsDelta",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「位置に delta を足し、id と向きは変えない」
bool FCubelithGameMovePlacementAddsDeltaTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FPlacement Before = Place(2, 7, Cubelith::Vec3(1, -2, 3));
	const Cubelith::FPlacement After = Cubelith::MovePlacement(Before, Cubelith::Vec3(0, 1, -1));

	TestEqual(TEXT("id"), After.PieceId, 2);
	TestEqual(TEXT("向き"), After.Orientation, 7);
	CheckVec3(*this, TEXT("移動後の位置"), After.Position, Cubelith::Vec3(1, -1, 2));
	// 元の配置は書き換えない
	CheckVec3(*this, TEXT("元の配置の位置"), Before.Position, Cubelith::Vec3(1, -2, 3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameMovePlacementZeroDeltaTest, "CUBELITH.Core.Game.MovePlacementZeroDelta",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「ゼロ移動は同じ位置になる」
bool FCubelithGameMovePlacementZeroDeltaTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FPlacement Before = Place(0, 0, Cubelith::Vec3(4, 5, 6));
	CheckVec3(*this, TEXT("ゼロ移動"), Cubelith::MovePlacement(Before, Cubelith::Vec3(0, 0, 0)).Position, Before.Position);
	return true;
}

// ---- rotatePlacement ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameRotatePlacementFourTurnsTest, "CUBELITH.Core.Game.RotatePlacementFourTurns",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「同じ軸に 4 回まわすと元の向きに戻る」
bool FCubelithGameRotatePlacementFourTurnsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FPlacement Start = Place(1, 5, Cubelith::Vec3(2, 0, -3));
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		for (const int32 Dir : Directions)
		{
			Cubelith::FPlacement Current = Start;
			for (int32 Turn = 0; Turn < 4; ++Turn)
			{
				Current = Cubelith::RotatePlacement(Current, Axes[AxisIndex], Dir);
			}
			TestEqual(*FString::Printf(TEXT("%s 軸 dir %d を 4 回まわした向き"), AxisNames[AxisIndex], Dir),
				Current.Orientation, Start.Orientation);
			CheckVec3(*this, FString::Printf(TEXT("%s 軸 dir %d を 4 回まわした位置"), AxisNames[AxisIndex], Dir),
				Current.Position, Start.Position);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameRotatePlacementCancelsTest, "CUBELITH.Core.Game.RotatePlacementCancels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「+1 と -1 は打ち消し合う」
bool FCubelithGameRotatePlacementCancelsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FPlacement Start = Place(0, 11, Cubelith::Vec3(0, 0, 0));
	const Cubelith::FPlacement Plus = Cubelith::RotatePlacement(Start, Cubelith::EAxis::Y, 1);
	const Cubelith::FPlacement Back = Cubelith::RotatePlacement(Plus, Cubelith::EAxis::Y, -1);

	TestEqual(TEXT("打ち消した後の向き"), Back.Orientation, 11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameRotatePlacementOriginFixedTest, "CUBELITH.Core.Game.RotatePlacementOriginFixed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「局所原点（= position のマス）は回転しても動かない」
bool FCubelithGameRotatePlacementOriginFixedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	if (Generated.Pieces.Num() == 0 || Generated.Solution.Num() == 0)
	{
		AddError(TEXT("テスト: ピースが無い"));
		return false;
	}

	const Cubelith::FPiece& Piece = Generated.Pieces[0];
	const Cubelith::FPlacement& Placement = Generated.Solution[0];
	const Cubelith::FPlacement Rotated = Cubelith::RotatePlacement(Placement, Cubelith::EAxis::X, 1);

	// 局所原点は (0,0,0) なので、回した後も position のマスを必ず占める
	const TArray<Cubelith::FVec3> Occupied = Cubelith::PlacedVoxels(Piece, Rotated);
	TestTrue(TEXT("回転後も position のマスを占める"), Occupied.Contains(Placement.Position));
	// ボクセル数は変わらない
	TestEqual(TEXT("ボクセル数"), Occupied.Num(), Piece.Voxels.Num());
	return true;
}

// ---- replacePlacement ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameReplacePlacementKeepsOrderTest, "CUBELITH.Core.Game.ReplacePlacementKeepsOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「対象だけを差し替え、並びは保つ」
bool FCubelithGameReplacePlacementKeepsOrderTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	TArray<Cubelith::FPlacement> List;
	List.Add(Place(0, 0, Cubelith::Vec3(0, 0, 0)));
	List.Add(Place(1, 0, Cubelith::Vec3(1, 0, 0)));
	List.Add(Place(2, 0, Cubelith::Vec3(2, 0, 0)));

	const Cubelith::FPlacement Next = Place(1, 3, Cubelith::Vec3(9, 9, 9));
	const TArray<Cubelith::FPlacement> Result = Cubelith::ReplacePlacement(List, Next);

	if (Result.Num() != 3)
	{
		AddError(FString::Printf(TEXT("差し替え後の件数が 3 でない: %d"), Result.Num()));
		return false;
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("[%d] の id"), Index), Result[Index].PieceId, Index);
	}
	CheckSamePlacement(*this, TEXT("差し替えた [1]"), Result[1], Next);
	CheckSamePlacement(*this, TEXT("触っていない [0]"), Result[0], List[0]);
	// 元の配列は書き換えない（TS の list[1]?.orientation が 0 のままであること）
	TestEqual(TEXT("元の配列の [1] の向き"), List[1].Orientation, 0);
	return true;
}

// ---- createGame ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameSolvedAtSolutionTest, "CUBELITH.Core.Game.SolvedAtSolution",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「解答配置なら solved が真、1 マスずらすと偽になる」
bool FCubelithGameSolvedAtSolutionTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	Cubelith::FGame Game = Cubelith::CreateGame(Generated.Pieces, N, Generated.Solution);

	TestTrue(TEXT("解答配置はクリア"), Game.Solved());
	Game.Move(0, Cubelith::Vec3(1, 0, 0));
	TestFalse(TEXT("1 マスずらすとクリアでない"), Game.Solved());
	Game.Move(0, Cubelith::Vec3(-1, 0, 0));
	TestTrue(TEXT("戻すと再びクリア"), Game.Solved());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameResetToSolutionSolvesTest, "CUBELITH.Core.Game.ResetToSolutionSolves",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「散らし配置から解答配置に戻すとクリアになる」
bool FCubelithGameResetToSolutionSolvesTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	const TArray<Cubelith::FPlacement> Scattered = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
	Cubelith::FGame Game = Cubelith::CreateGame(Generated.Pieces, N, Scattered);

	TestFalse(TEXT("散らし配置はクリアでない"), Game.Solved());
	Game.Reset(Generated.Solution);
	TestTrue(TEXT("解答配置に戻すとクリア"), Game.Solved());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameOnChangeOnEachUpdateTest, "CUBELITH.Core.Game.OnChangeOnEachUpdate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「動かすたびに onChange がクリア判定つきで呼ばれる」
bool FCubelithGameOnChangeOnEachUpdateTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	FChangeLog Log;
	Cubelith::FGame Game(Generated.Pieces, N, Generated.Solution, MakeListener(Log));

	// 構築時には呼ばない
	TestEqual(TEXT("構築時の呼び出し回数"), Log.Count, 0);

	Game.Move(1, Cubelith::Vec3(0, 5, 0));
	TestEqual(TEXT("move 後の呼び出し回数"), Log.Count, 1);
	if (Log.Solved.Num() >= 1)
	{
		TestFalse(TEXT("move 後の判定"), Log.Solved[0]);
	}

	Game.Rotate(1, Cubelith::EAxis::Y, 1);
	TestEqual(TEXT("rotate 後の呼び出し回数"), Log.Count, 2);

	Game.Reset(Generated.Solution);
	TestEqual(TEXT("reset 後の呼び出し回数"), Log.Count, 3);
	if (Log.Solved.Num() >= 3)
	{
		TestTrue(TEXT("reset 後の判定"), Log.Solved[2]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameOnChangeMatchesPlacementsTest, "CUBELITH.Core.Game.OnChangeMatchesPlacements",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「onChange に渡る配置は placements() と一致し、判定は isSolved と同じ」
bool FCubelithGameOnChangeMatchesPlacementsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	FChangeLog Log;
	Cubelith::FGame Game(Generated.Pieces, N, Generated.Solution, MakeListener(Log));

	Game.Move(2, Cubelith::Vec3(-3, 1, 2));

	const TArrayView<const Cubelith::FPlacement> Placements = Game.Placements();
	if (Log.LastPlacements.Num() != Placements.Num())
	{
		AddError(FString::Printf(TEXT("onChange に渡った件数が placements() と違う（%d / %d）"),
			Log.LastPlacements.Num(), Placements.Num()));
		return false;
	}
	for (int32 Index = 0; Index < Placements.Num(); ++Index)
	{
		CheckSamePlacement(*this, FString::Printf(TEXT("onChange に渡った [%d]"), Index), Log.LastPlacements[Index], Placements[Index]);
	}
	TestTrue(TEXT("判定は IsSolved と同じ"), Game.Solved() == Cubelith::IsSolved(Generated.Pieces, Placements, N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameMoveRotateAffectsTargetOnlyTest, "CUBELITH.Core.Game.MoveRotateAffectsTargetOnly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「move / rotate は対象以外の配置を変えない」
bool FCubelithGameMoveRotateAffectsTargetOnlyTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	Cubelith::FGame Game = Cubelith::CreateGame(Generated.Pieces, N, Generated.Solution);

	TArray<Cubelith::FPlacement> Others;
	for (const Cubelith::FPlacement& Placement : Generated.Solution)
	{
		if (Placement.PieceId != 0)
		{
			Others.Add(Placement);
		}
	}

	Game.Move(0, Cubelith::Vec3(2, -1, 0));
	Game.Rotate(0, Cubelith::EAxis::Z, -1);

	for (const Cubelith::FPlacement& Before : Others)
	{
		const Cubelith::FPlacement* After = Game.PlacementOf(Before.PieceId);
		if (After == nullptr)
		{
			AddError(FString::Printf(TEXT("テスト: ピース %d の配置が無い"), Before.PieceId));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("ピース %d の向き"), Before.PieceId), After->Orientation, Before.Orientation);
		TestTrue(*FString::Printf(TEXT("ピース %d の位置"), Before.PieceId), Cubelith::EqualsVec3(After->Position, Before.Position));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGamePlacementOfCurrentTest, "CUBELITH.Core.Game.PlacementOfCurrent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「placementOf は現在の配置を返し、未知の id では undefined」
bool FCubelithGamePlacementOfCurrentTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	Cubelith::FGame Game = Cubelith::CreateGame(Generated.Pieces, N, Generated.Solution);

	const Cubelith::FPlacement* Answer = Find(Generated.Solution, 0);
	if (Answer == nullptr)
	{
		AddError(TEXT("テスト: ピース 0 の解答配置が無い"));
		return false;
	}

	const Cubelith::FVec3 Moved = Cubelith::Vec3(1, 2, 3);
	Game.Move(0, Moved);

	const Cubelith::FPlacement* Placement = Game.PlacementOf(0);
	if (Placement == nullptr)
	{
		AddError(TEXT("move の後にピース 0 の配置が無い"));
		return false;
	}
	CheckVec3(*this, TEXT("move 後の位置"), Placement->Position, Cubelith::AddVec3(Answer->Position, Moved));

	// TS の undefined は nullptr で表す（U1 共通の約束）
	TestTrue(TEXT("未知の id の配置は nullptr"), Game.PlacementOf(999) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameSolvableByRotateAndStepTest, "CUBELITH.Core.Game.SolvableByRotateAndStep",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「散らし配置から ±90 度回転と 1 マス移動だけでクリアまで持っていける」
bool FCubelithGameSolvableByRotateAndStepTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	const TArray<Cubelith::FPlacement> Scattered = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
	Cubelith::FGame Game = Cubelith::CreateGame(Generated.Pieces, N, Scattered);

	TestFalse(TEXT("散らし配置はクリアでない"), Game.Solved());

	for (const Cubelith::FPlacement& Target : Generated.Solution)
	{
		// 回転: 現在の向きから目標の向きまでを ±90 度の 1 手ずつで辿る（UI ボタンと同じ操作）
		Cubelith::FPlacement Start;
		if (!CopyPlacement(*this, Game, Target.PieceId, Start))
		{
			return false;
		}

		bool bFound = false;
		const TArray<FRotationStep> Path = RotationPath(Start.Orientation, Target.Orientation, bFound);
		if (!bFound)
		{
			AddError(FString::Printf(TEXT("テスト: 向き %d から %d へ辿れない"), Start.Orientation, Target.Orientation));
			return false;
		}
		for (const FRotationStep& Step : Path)
		{
			Game.Rotate(Target.PieceId, Step.Axis, Step.Dir);
		}

		Cubelith::FPlacement Turned;
		if (!CopyPlacement(*this, Game, Target.PieceId, Turned))
		{
			return false;
		}
		TestEqual(*FString::Printf(TEXT("ピース %d を回した後の向き"), Target.PieceId), Turned.Orientation, Target.Orientation);

		// 移動: 1 マスずつ（ドラッグ 1 段分と同じ粒度）
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			for (;;)
			{
				Cubelith::FPlacement Now;
				if (!CopyPlacement(*this, Game, Target.PieceId, Now))
				{
					return false;
				}
				const int32 Diff = Component(Target.Position, Axes[AxisIndex]) - Component(Now.Position, Axes[AxisIndex]);
				if (Diff == 0)
				{
					break;
				}
				Game.Move(Target.PieceId, UnitStep(Axes[AxisIndex], Diff > 0 ? 1 : -1));
			}
		}
	}

	TestTrue(TEXT("回転と 1 マス移動だけでクリアできる"), Game.Solved());
	return true;
}

// ---- Game の固定（ロック）と place ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGamePlaceSetsOrientationAndPositionTest, "CUBELITH.Core.Game.PlaceSetsOrientationAndPosition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「place は位置と向きを直接置く」
bool FCubelithGamePlaceSetsOrientationAndPositionTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	const TArray<Cubelith::FPlacement> Initial = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
	FChangeLog Log;
	Cubelith::FGame Game(Generated.Pieces, N, Initial, MakeListener(Log));

	const Cubelith::FPlacement* Answer = Find(Generated.Solution, 1);
	if (Answer == nullptr)
	{
		AddError(TEXT("テスト: ピース 1 の解答配置が無い"));
		return false;
	}
	Game.Place(1, Answer->Orientation, Answer->Position);

	Cubelith::FPlacement Placed;
	if (!CopyPlacement(*this, Game, 1, Placed))
	{
		return false;
	}
	TestEqual(TEXT("place した向き"), Placed.Orientation, Answer->Orientation);
	CheckVec3(*this, TEXT("place した位置"), Placed.Position, Answer->Position);
	TestEqual(TEXT("onChange の回数"), Log.Count, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameLockUnlockDoesNotNotifyTest, "CUBELITH.Core.Game.LockUnlockDoesNotNotify",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「lock / unlock は配置を変えないので onChange を呼ばない」
bool FCubelithGameLockUnlockDoesNotNotifyTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	const TArray<Cubelith::FPlacement> Initial = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
	FChangeLog Log;
	Cubelith::FGame Game(Generated.Pieces, N, Initial, MakeListener(Log));

	Game.Lock(0, Cubelith::ELockKind::Manual);
	Game.Unlock(0);
	TestEqual(TEXT("onChange の回数"), Log.Count, 0);
	CheckUnlocked(*this, TEXT("unlock 後の固定"), Game.LockKindOf(0));

	// 移植しなかった異常系テスト「未知のピース id は例外」の正常系だった部分（lockKindOf は例外にならず null）
	CheckUnlocked(*this, TEXT("未知の id の固定"), Game.LockKindOf(99));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameLockedPieceIgnoresUpdatesTest, "CUBELITH.Core.Game.LockedPieceIgnoresUpdates",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「固定中は move / rotate / place が効かず onChange も呼ばれない」
bool FCubelithGameLockedPieceIgnoresUpdatesTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::ELockKind Kinds[2] = { Cubelith::ELockKind::Manual, Cubelith::ELockKind::Hint };
	for (const Cubelith::ELockKind Kind : Kinds)
	{
		const FString KindName = LockKindName(TOptional<Cubelith::ELockKind>(Kind));

		const Cubelith::FGeneratedPuzzle Generated = Puzzle();
		const TArray<Cubelith::FPlacement> Initial = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
		FChangeLog Log;
		Cubelith::FGame Game(Generated.Pieces, N, Initial, MakeListener(Log));

		Cubelith::FPlacement Before;
		if (!CopyPlacement(*this, Game, 0, Before))
		{
			return false;
		}

		Game.Lock(0, Kind);
		Log.Clear();

		Game.Move(0, Cubelith::Vec3(1, 0, 0));
		Game.Rotate(0, Cubelith::EAxis::Y, 1);
		const Cubelith::FPlacement* Answer = Find(Generated.Solution, 0);
		if (Answer == nullptr)
		{
			AddError(TEXT("テスト: ピース 0 の解答配置が無い"));
			return false;
		}
		Game.Place(0, Answer->Orientation, Answer->Position);

		Cubelith::FPlacement After;
		if (!CopyPlacement(*this, Game, 0, After))
		{
			return false;
		}
		CheckSamePlacement(*this, FString::Printf(TEXT("%s で固定中の配置"), *KindName), After, Before);
		TestEqual(*FString::Printf(TEXT("%s で固定中の onChange の回数"), *KindName), Log.Count, 0);
		CheckLockKind(*this, FString::Printf(TEXT("%s の固定"), *KindName), Game.LockKindOf(0), TOptional<Cubelith::ELockKind>(Kind));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameUnlockedPieceStillMovableTest, "CUBELITH.Core.Game.UnlockedPieceStillMovable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「固定していないピースは固定中のピースがあっても動かせる」
bool FCubelithGameUnlockedPieceStillMovableTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	const TArray<Cubelith::FPlacement> Initial = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
	FChangeLog Log;
	Cubelith::FGame Game(Generated.Pieces, N, Initial, MakeListener(Log));

	Game.Lock(0, Cubelith::ELockKind::Manual);

	Cubelith::FPlacement Before;
	if (!CopyPlacement(*this, Game, 1, Before))
	{
		return false;
	}
	Game.Move(1, Cubelith::Vec3(2, 0, 0));

	Cubelith::FPlacement After;
	if (!CopyPlacement(*this, Game, 1, After))
	{
		return false;
	}
	CheckVec3(*this, TEXT("未固定のピースの移動後の位置"), After.Position, Cubelith::AddVec3(Before.Position, Cubelith::Vec3(2, 0, 0)));
	TestEqual(TEXT("onChange の回数"), Log.Count, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameMovableAfterUnlockTest, "CUBELITH.Core.Game.MovableAfterUnlock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「unlock 後は再び動かせる」
bool FCubelithGameMovableAfterUnlockTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	const TArray<Cubelith::FPlacement> Initial = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
	FChangeLog Log;
	Cubelith::FGame Game(Generated.Pieces, N, Initial, MakeListener(Log));

	Game.Lock(0, Cubelith::ELockKind::Manual);
	Game.Move(0, Cubelith::Vec3(1, 0, 0));
	TestEqual(TEXT("固定中の onChange の回数"), Log.Count, 0);

	Game.Unlock(0);

	Cubelith::FPlacement Before;
	if (!CopyPlacement(*this, Game, 0, Before))
	{
		return false;
	}
	Game.Move(0, Cubelith::Vec3(1, 0, 0));

	Cubelith::FPlacement After;
	if (!CopyPlacement(*this, Game, 0, After))
	{
		return false;
	}
	CheckVec3(*this, TEXT("unlock 後の移動"), After.Position, Cubelith::AddVec3(Before.Position, Cubelith::Vec3(1, 0, 0)));
	TestEqual(TEXT("unlock 後の onChange の回数"), Log.Count, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameHintLockNotReleasedTest, "CUBELITH.Core.Game.HintLockNotReleased",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「'hint' の固定は unlock で解除されない」
bool FCubelithGameHintLockNotReleasedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	const TArray<Cubelith::FPlacement> Initial = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
	Cubelith::FGame Game = Cubelith::CreateGame(Generated.Pieces, N, Initial);

	Game.Lock(2, Cubelith::ELockKind::Hint);
	Game.Unlock(2);
	CheckLockKind(*this, TEXT("unlock しても Hint のまま"), Game.LockKindOf(2),
		TOptional<Cubelith::ELockKind>(Cubelith::ELockKind::Hint));

	Cubelith::FPlacement Before;
	if (!CopyPlacement(*this, Game, 2, Before))
	{
		return false;
	}
	Game.Move(2, Cubelith::Vec3(3, 0, 0));

	Cubelith::FPlacement After;
	if (!CopyPlacement(*this, Game, 2, After))
	{
		return false;
	}
	CheckSamePlacement(*this, TEXT("Hint の固定中は動かない"), After, Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameLockOverwritesKindTest, "CUBELITH.Core.Game.LockOverwritesKind",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「lock は同じピースの固定の種類を上書きできる」
bool FCubelithGameLockOverwritesKindTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	const TArray<Cubelith::FPlacement> Initial = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
	Cubelith::FGame Game = Cubelith::CreateGame(Generated.Pieces, N, Initial);

	Game.Lock(1, Cubelith::ELockKind::Manual);
	CheckLockKind(*this, TEXT("Manual で固定"), Game.LockKindOf(1), TOptional<Cubelith::ELockKind>(Cubelith::ELockKind::Manual));
	Game.Lock(1, Cubelith::ELockKind::Hint);
	CheckLockKind(*this, TEXT("Hint で上書き"), Game.LockKindOf(1), TOptional<Cubelith::ELockKind>(Cubelith::ELockKind::Hint));
	Game.Unlock(1);
	CheckLockKind(*this, TEXT("上書き後は unlock できない"), Game.LockKindOf(1), TOptional<Cubelith::ELockKind>(Cubelith::ELockKind::Hint));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameLockedIdsContainsAllTest, "CUBELITH.Core.Game.LockedIdsContainsAll",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「lockedIds() は固定中のピースをすべて含む（順序に依存しない）」
bool FCubelithGameLockedIdsContainsAllTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	const TArray<Cubelith::FPlacement> Initial = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
	Cubelith::FGame Game = Cubelith::CreateGame(Generated.Pieces, N, Initial);

	CheckIds(*this, TEXT("固定なし"), Game.LockedIds(), TArray<int32>{});

	// 固定した順（2 → 0）と関係なく、LockedIds() は昇順で返す
	Game.Lock(2, Cubelith::ELockKind::Manual);
	Game.Lock(0, Cubelith::ELockKind::Hint);
	CheckIds(*this, TEXT("2 と 0 を固定"), Game.LockedIds(), TArray<int32>{ 0, 2 });

	Game.Unlock(2);
	CheckIds(*this, TEXT("2 を解除"), Game.LockedIds(), TArray<int32>{ 0 });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameResetClearsManualKeepsHintTest, "CUBELITH.Core.Game.ResetClearsManualKeepsHint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「reset は 'manual' を解除して 'hint' を残す」
bool FCubelithGameResetClearsManualKeepsHintTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	const TArray<Cubelith::FPlacement> Initial = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
	Cubelith::FGame Game = Cubelith::CreateGame(Generated.Pieces, N, Initial);

	Game.Lock(0, Cubelith::ELockKind::Manual);
	Game.Lock(1, Cubelith::ELockKind::Hint);
	Game.Lock(2, Cubelith::ELockKind::Manual);

	Game.Reset(Cubelith::ScatterPlacements(Generated.Pieces, N, Seed + 1));

	CheckUnlocked(*this, TEXT("reset 後のピース 0"), Game.LockKindOf(0));
	CheckLockKind(*this, TEXT("reset 後のピース 1"), Game.LockKindOf(1), TOptional<Cubelith::ELockKind>(Cubelith::ELockKind::Hint));
	CheckUnlocked(*this, TEXT("reset 後のピース 2"), Game.LockKindOf(2));
	CheckIds(*this, TEXT("reset 後の固定中の id"), Game.LockedIds(), TArray<int32>{ 1 });

	// 解除されたピースは動かせて、Hint のピースは動かせないまま
	Cubelith::FPlacement BeforeHint;
	if (!CopyPlacement(*this, Game, 1, BeforeHint))
	{
		return false;
	}
	Game.Move(1, Cubelith::Vec3(1, 0, 0));
	Cubelith::FPlacement AfterHint;
	if (!CopyPlacement(*this, Game, 1, AfterHint))
	{
		return false;
	}
	CheckSamePlacement(*this, TEXT("Hint のピースは動かない"), AfterHint, BeforeHint);

	Cubelith::FPlacement BeforeManual;
	if (!CopyPlacement(*this, Game, 0, BeforeManual))
	{
		return false;
	}
	Game.Move(0, Cubelith::Vec3(1, 0, 0));
	Cubelith::FPlacement AfterManual;
	if (!CopyPlacement(*this, Game, 0, AfterManual))
	{
		return false;
	}
	TestTrue(TEXT("解除されたピースは動く"), AfterManual != BeforeManual);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGameHintPlacingAllSolvesTest, "CUBELITH.Core.Game.HintPlacingAllSolves",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// TS: 「ヒントで全ピースを解答位置へ置くとクリアになる」
bool FCubelithGameHintPlacingAllSolvesTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests::GameTestDetail;

	const Cubelith::FGeneratedPuzzle Generated = Puzzle();
	const TArray<Cubelith::FPlacement> Initial = Cubelith::ScatterPlacements(Generated.Pieces, N, Seed);
	Cubelith::FGame Game = Cubelith::CreateGame(Generated.Pieces, N, Initial);

	for (const Cubelith::FPlacement& Answer : Generated.Solution)
	{
		Game.Place(Answer.PieceId, Answer.Orientation, Answer.Position);
		Game.Lock(Answer.PieceId, Cubelith::ELockKind::Hint);
	}
	TestTrue(TEXT("全ピースをヒントで置くとクリア"), Game.Solved());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
