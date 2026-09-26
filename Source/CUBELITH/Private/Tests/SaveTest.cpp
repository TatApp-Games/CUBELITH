// CubelithSave（セーブデータの形と検証・更新。RULES.md 3.8・Docs/SPEC_UE.md 4 章）のテスト。
// 移植元 WebMock/src/ui/save.ts に対応するテストは WebMock にも無いので、確かめるのは
// 既定値 / DifficultyKey の形 / 検証で弾く条件 / RecordClear / ProgressFitsPieces / 壊れたデータの扱い。
// スロットへの読み書き（Cubelith::LoadSaveData / StoreSaveData）は実際のファイルを触るのでテストしない
// （CubelithSaveGame.h の「解釈:」。エディタの Saved/ を汚さないため）。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithSave.h"
#include "Game.h"
#include "Generate.h"
#include "Grid.h"

namespace CubelithRenderTests
{
	// SaveTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace SaveTestDetail
	{
		FCubelithSavedDifficulty Difficulty(int32 SpaceSize, int32 PieceCount, bool bAllowRotation)
		{
			FCubelithSavedDifficulty Result;
			Result.SpaceSize = SpaceSize;
			Result.PieceCount = PieceCount;
			Result.bAllowRotation = bAllowRotation;
			return Result;
		}

		FCubelithSavedPlacement Placement(int32 PieceId, int32 Orientation, const FIntVector& Position)
		{
			FCubelithSavedPlacement Result;
			Result.PieceId = PieceId;
			Result.Orientation = Orientation;
			Result.Position = Position;
			return Result;
		}

		FCubelithSavedLock Lock(int32 PieceId, ECubelithSavedLockKind Kind)
		{
			FCubelithSavedLock Result;
			Result.PieceId = PieceId;
			Result.Kind = Kind;
			return Result;
		}

		/** 検証を通る盤面（N=3 / M=4 / 回転なし、配置 4 個・固定 1 個・残り 2） */
		FCubelithSavedProgress ValidProgress()
		{
			FCubelithSavedProgress Progress;
			Progress.Difficulty = Difficulty(3, 4, false);
			Progress.Seed = 20260904;
			Progress.Placements = {
				Placement(0, 0, FIntVector(0, 0, 0)),
				Placement(1, 5, FIntVector(1, 0, 0)),
				Placement(2, 23, FIntVector(-4, 2, 7)),
				Placement(3, 0, FIntVector(0, 5, 0)),
			};
			Progress.Locks = { Lock(1, ECubelithSavedLockKind::Hint) };
			Progress.Remaining = 2;
			return Progress;
		}

		/** 盤面つき・クリア回数ありのセーブデータ */
		FCubelithSaveData DataWithProgress()
		{
			FCubelithSaveData Data;
			Data.Difficulty = Difficulty(3, 4, false);
			Data.bHasProgress = true;
			Data.Progress = ValidProgress();
			Data.Clears.Total = 3;
			Data.Clears.ByDifficulty.Add(TEXT("3-4-0"), 2);
			Data.Clears.ByDifficulty.Add(TEXT("4-6-1"), 1);
			return Data;
		}
	}
}

// 1. 既定値（RULES.md 3.1 の N=3 / M=4 / 回転なし・途中の盤面なし・クリア回数 0）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSaveDefaultsTest, "CUBELITH.Render.Save.Defaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSaveDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SaveTestDetail;

	const FCubelithSaveData Data = Cubelith::DefaultSaveData();
	TestEqual(TEXT("既定の N"), Data.Difficulty.SpaceSize, 3);
	TestEqual(TEXT("既定の M"), Data.Difficulty.PieceCount, 4);
	TestFalse(TEXT("既定のパズルの回転はなし"), Data.Difficulty.bAllowRotation);
	TestFalse(TEXT("途中の盤面は無い"), Data.bHasProgress);
	TestEqual(TEXT("クリア回数の合計"), Data.Clears.Total, 0);
	TestEqual(TEXT("難易度ごとのクリア回数は空"), Data.Clears.ByDifficulty.Num(), 0);
	TestTrue(TEXT("既定の難易度は検証を通る"), Cubelith::IsValidSavedDifficulty(Data.Difficulty));
	TestEqual(TEXT("既定の難易度のクリア回数は 0"), Cubelith::ClearCountOf(Data, Data.Difficulty), 0);

	return true;
}

// 2. DifficultyKey の形（N-M-回転）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSaveDifficultyKeyTest, "CUBELITH.Render.Save.DifficultyKey",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSaveDifficultyKeyTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SaveTestDetail;

	TestEqual(TEXT("N=3 M=4 回転なし"), Cubelith::DifficultyKey(Difficulty(3, 4, false)), FString(TEXT("3-4-0")));
	TestEqual(TEXT("N=3 M=4 回転あり"), Cubelith::DifficultyKey(Difficulty(3, 4, true)), FString(TEXT("3-4-1")));
	TestEqual(TEXT("N=7 M=27 回転あり"), Cubelith::DifficultyKey(Difficulty(7, 27, true)), FString(TEXT("7-27-1")));

	return true;
}

// 3. 難易度の検証（N は 3..7、M は 2..MaxPieces(N)）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSaveValidDifficultyTest, "CUBELITH.Render.Save.ValidDifficulty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSaveValidDifficultyTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SaveTestDetail;

	struct FCase
	{
		const TCHAR* Description;
		int32 SpaceSize;
		int32 PieceCount;
		bool bExpected;
	};

	const FCase Cases[] = {
		{ TEXT("既定"), 3, 4, true },
		{ TEXT("M の下限"), 3, 2, true },
		{ TEXT("N の上限と M の上限"), 7, 27, true },
		{ TEXT("N が下限未満"), 2, 4, false },
		{ TEXT("N が上限超え"), 8, 4, false },
		{ TEXT("N が 0"), 0, 4, false },
		{ TEXT("N が負"), -1, 4, false },
		{ TEXT("M が下限未満"), 3, 1, false },
		{ TEXT("M が負"), 3, -1, false },
		{ TEXT("M が N の上限超え（N=3 の上限は 7）"), 3, 8, false },
		{ TEXT("M が N=7 の上限超え"), 7, 28, false },
	};

	for (const FCase& Case : Cases)
	{
		const bool Actual = Cubelith::IsValidSavedDifficulty(Difficulty(Case.SpaceSize, Case.PieceCount, false));
		if (Actual != Case.bExpected)
		{
			AddError(FString::Printf(TEXT("%s（N=%d M=%d）の検証が %s（期待 %s）"),
				Case.Description, Case.SpaceSize, Case.PieceCount,
				Actual ? TEXT("true") : TEXT("false"), Case.bExpected ? TEXT("true") : TEXT("false")));
		}
	}

	return true;
}

// 4. 盤面の検証で弾く条件（向きが 24 以上・id の重複・固定の id が配置に無い・配置数が M と食い違う など）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSaveValidProgressTest, "CUBELITH.Render.Save.ValidProgress",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSaveValidProgressTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SaveTestDetail;

	TestTrue(TEXT("正しい盤面は通る"), Cubelith::IsValidSavedProgress(ValidProgress()));

	struct FCase
	{
		const TCHAR* Description;
		TFunction<void(FCubelithSavedProgress&)> Break;
	};

	const FCase Cases[] = {
		{ TEXT("難易度が範囲外"),
			[](FCubelithSavedProgress& P) { P.Difficulty.SpaceSize = 9; } },
		{ TEXT("M が範囲外"),
			[](FCubelithSavedProgress& P) { P.Difficulty.PieceCount = 0; } },
		{ TEXT("シードが負（保存されていない）"),
			[](FCubelithSavedProgress& P) { P.Seed = -1; } },
		{ TEXT("シードが uint32 の上限超え"),
			[](FCubelithSavedProgress& P) { P.Seed = 4294967296; } },
		{ TEXT("配置が空"),
			[](FCubelithSavedProgress& P) { P.Placements.Empty(); } },
		{ TEXT("配置数が M より少ない"),
			[](FCubelithSavedProgress& P) { P.Placements.RemoveAt(3); } },
		{ TEXT("配置数が M より多い"),
			[](FCubelithSavedProgress& P) { P.Placements.Add(Placement(4, 0, FIntVector(3, 3, 3))); } },
		{ TEXT("向きが 24"),
			[](FCubelithSavedProgress& P) { P.Placements[1].Orientation = 24; } },
		{ TEXT("向きが負"),
			[](FCubelithSavedProgress& P) { P.Placements[1].Orientation = -1; } },
		{ TEXT("PieceId が重複"),
			[](FCubelithSavedProgress& P) { P.Placements[2].PieceId = 1; } },
		{ TEXT("PieceId が負"),
			[](FCubelithSavedProgress& P) { P.Placements[2].PieceId = -1; } },
		{ TEXT("固定の id が配置に無い"),
			[](FCubelithSavedProgress& P) { P.Locks[0].PieceId = 9; } },
		{ TEXT("固定の id が重複"),
			[](FCubelithSavedProgress& P) { P.Locks.Add(Lock(1, ECubelithSavedLockKind::Manual)); } },
		{ TEXT("残りピース数が負"),
			[](FCubelithSavedProgress& P) { P.Remaining = -1; } },
		{ TEXT("残りピース数が配置数超え"),
			[](FCubelithSavedProgress& P) { P.Remaining = 5; } },
	};

	for (const FCase& Case : Cases)
	{
		FCubelithSavedProgress Progress = ValidProgress();
		Case.Break(Progress);
		if (Cubelith::IsValidSavedProgress(Progress))
		{
			AddError(FString::Printf(TEXT("%s の盤面が検証を通ってしまった"), Case.Description));
		}
	}

	// 境界: 残りピース数は 0 と配置数そのものなら通る
	{
		FCubelithSavedProgress Progress = ValidProgress();
		Progress.Remaining = 0;
		TestTrue(TEXT("残り 0 は通る"), Cubelith::IsValidSavedProgress(Progress));
		Progress.Remaining = Progress.Placements.Num();
		TestTrue(TEXT("残りが配置数そのものなら通る"), Cubelith::IsValidSavedProgress(Progress));
	}

	// 固定が 1 つも無い盤面も通る（固定中のピースだけを持つので空が普通）
	{
		FCubelithSavedProgress Progress = ValidProgress();
		Progress.Locks.Empty();
		TestTrue(TEXT("固定なしは通る"), Cubelith::IsValidSavedProgress(Progress));
	}

	return true;
}

// 5. RecordClear で合計と難易度ごとが 1 増え、途中の盤面が消える（RULES.md 3.8）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSaveRecordClearTest, "CUBELITH.Render.Save.RecordClear",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSaveRecordClearTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SaveTestDetail;

	// 記録のある難易度
	{
		FCubelithSaveData Data = DataWithProgress();
		Cubelith::RecordClear(Data, Difficulty(3, 4, false));
		TestEqual(TEXT("合計が 1 増える"), Data.Clears.Total, 4);
		TestEqual(TEXT("その難易度の回数が 1 増える"),
			Cubelith::ClearCountOf(Data, Difficulty(3, 4, false)), 3);
		TestEqual(TEXT("他の難易度の回数は変わらない"),
			Cubelith::ClearCountOf(Data, Difficulty(4, 6, true)), 1);
		TestFalse(TEXT("途中の盤面が消える"), Data.bHasProgress);
		TestEqual(TEXT("途中の盤面の中身も既定に戻る"), Data.Progress.Placements.Num(), 0);
		TestTrue(TEXT("最後に選んだ難易度はクリアした難易度"),
			Data.Difficulty == Difficulty(3, 4, false));
	}

	// 記録の無い難易度（0 から 1 になる。最後に選んだ難易度もそちらへ揃う）
	{
		FCubelithSaveData Data = DataWithProgress();
		const FCubelithSavedDifficulty Cleared = Difficulty(5, 11, true);
		Cubelith::RecordClear(Data, Cleared);
		TestEqual(TEXT("合計が 1 増える"), Data.Clears.Total, 4);
		TestEqual(TEXT("初めての難易度は 1 になる"), Cubelith::ClearCountOf(Data, Cleared), 1);
		TestEqual(TEXT("元からあった難易度は変わらない"),
			Cubelith::ClearCountOf(Data, Difficulty(3, 4, false)), 2);
		TestTrue(TEXT("最後に選んだ難易度が揃う"), Data.Difficulty == Cleared);
	}

	// 既定値から 2 回続けて記録する
	{
		FCubelithSaveData Data = Cubelith::DefaultSaveData();
		const FCubelithSavedDifficulty Cleared = Difficulty(3, 4, false);
		Cubelith::RecordClear(Data, Cleared);
		Cubelith::RecordClear(Data, Cleared);
		TestEqual(TEXT("合計 2"), Data.Clears.Total, 2);
		TestEqual(TEXT("難易度ごと 2"), Cubelith::ClearCountOf(Data, Cleared), 2);
	}

	return true;
}

// 6. ProgressFitsPieces（保存された盤面をそのまま初期配置として使えるか）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSaveProgressFitsPiecesTest, "CUBELITH.Render.Save.ProgressFitsPieces",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSaveProgressFitsPiecesTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SaveTestDetail;

	const FCubelithSavedProgress Progress = ValidProgress();

	{
		const int32 Ids[4] = { 0, 1, 2, 3 };
		TestTrue(TEXT("id がちょうど 1 回ずつ現れる"), Cubelith::ProgressFitsPieces(Progress, Ids));
	}
	{
		// 並びが違っても id の集合が同じなら使える
		const int32 Ids[4] = { 3, 1, 0, 2 };
		TestTrue(TEXT("並びが違っても使える"), Cubelith::ProgressFitsPieces(Progress, Ids));
	}
	{
		const int32 Ids[3] = { 0, 1, 2 };
		TestFalse(TEXT("ピース数が足りない"), Cubelith::ProgressFitsPieces(Progress, Ids));
	}
	{
		const int32 Ids[5] = { 0, 1, 2, 3, 4 };
		TestFalse(TEXT("ピース数が多い"), Cubelith::ProgressFitsPieces(Progress, Ids));
	}
	{
		const int32 Ids[4] = { 0, 1, 2, 9 };
		TestFalse(TEXT("配置に無い id がある"), Cubelith::ProgressFitsPieces(Progress, Ids));
	}
	{
		// 配置側に重複がある（数は合うが id が 1 回ずつではない）
		FCubelithSavedProgress Duplicated = Progress;
		Duplicated.Placements[3].PieceId = 2;
		const int32 Ids[4] = { 0, 1, 2, 3 };
		TestFalse(TEXT("配置に重複がある"), Cubelith::ProgressFitsPieces(Duplicated, Ids));
	}
	{
		// 生成したパズルの id とも合うこと（本番の使い方）
		const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(3, 4, 20260904u);
		TArray<int32> Ids;
		for (const Cubelith::FPiece& Piece : Puzzle.Pieces)
		{
			Ids.Add(Piece.Id);
		}
		TestTrue(TEXT("生成したパズルの id と合う"), Cubelith::ProgressFitsPieces(Progress, Ids));
	}

	return true;
}

// 7. 壊れたデータの扱い（SanitizeSaveData）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSaveSanitizeTest, "CUBELITH.Render.Save.Sanitize",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSaveSanitizeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SaveTestDetail;

	// 正しいデータはそのまま通る
	{
		const FCubelithSaveData Data = DataWithProgress();
		const FCubelithSaveData Result = Cubelith::SanitizeSaveData(Data);
		TestTrue(TEXT("難易度はそのまま"), Result.Difficulty == Data.Difficulty);
		TestTrue(TEXT("途中の盤面が残る"), Result.bHasProgress);
		TestEqual(TEXT("配置の数"), Result.Progress.Placements.Num(), 4);
		TestEqual(TEXT("クリア回数の合計"), Result.Clears.Total, 3);
		TestEqual(TEXT("難易度ごとのクリア回数"), Result.Clears.ByDifficulty.Num(), 2);
	}

	// 難易度が壊れている → まるごと既定値
	{
		FCubelithSaveData Data = DataWithProgress();
		Data.Difficulty.SpaceSize = 99;
		const FCubelithSaveData Result = Cubelith::SanitizeSaveData(Data);
		TestTrue(TEXT("難易度が既定に戻る"), Result.Difficulty == Difficulty(3, 4, false));
		TestFalse(TEXT("途中の盤面も捨てる"), Result.bHasProgress);
		TestEqual(TEXT("クリア回数も捨てる"), Result.Clears.Total, 0);
		TestEqual(TEXT("難易度ごとのクリア回数も捨てる"), Result.Clears.ByDifficulty.Num(), 0);
	}

	// 途中の盤面だけが壊れている → 盤面だけ捨てて難易度とクリア回数は生かす
	{
		FCubelithSaveData Data = DataWithProgress();
		Data.Progress.Placements[0].Orientation = 24;
		const FCubelithSaveData Result = Cubelith::SanitizeSaveData(Data);
		TestFalse(TEXT("途中の盤面を捨てる"), Result.bHasProgress);
		TestTrue(TEXT("難易度は生きる"), Result.Difficulty == Difficulty(3, 4, false));
		TestEqual(TEXT("クリア回数は生きる"), Result.Clears.Total, 3);
		TestEqual(TEXT("難易度ごとのクリア回数も生きる"),
			Cubelith::ClearCountOf(Result, Difficulty(3, 4, false)), 2);
	}

	// 盤面のピース数が難易度の M と食い違う（生成規則が変わった場合）→ 盤面だけ捨てる
	{
		FCubelithSaveData Data = DataWithProgress();
		Data.Progress.Difficulty.PieceCount = 5;
		const FCubelithSaveData Result = Cubelith::SanitizeSaveData(Data);
		TestFalse(TEXT("途中の盤面を捨てる"), Result.bHasProgress);
		TestEqual(TEXT("クリア回数は生きる"), Result.Clears.Total, 3);
	}

	// フラグが false なら中身は見ない（壊れていても難易度とクリア回数は生きる）
	{
		FCubelithSaveData Data = DataWithProgress();
		Data.bHasProgress = false;
		Data.Progress.Placements.Empty();
		const FCubelithSaveData Result = Cubelith::SanitizeSaveData(Data);
		TestFalse(TEXT("途中の盤面は無い"), Result.bHasProgress);
		TestEqual(TEXT("クリア回数は生きる"), Result.Clears.Total, 3);
	}

	// クリア回数: 負の合計は 0 に、負の回数のキーは落とす
	{
		FCubelithSaveData Data = DataWithProgress();
		Data.Clears.Total = -5;
		Data.Clears.ByDifficulty.Add(TEXT("5-11-0"), -1);
		const FCubelithSaveData Result = Cubelith::SanitizeSaveData(Data);
		TestEqual(TEXT("負の合計は 0"), Result.Clears.Total, 0);
		TestFalse(TEXT("負の回数のキーは落とす"), Result.Clears.ByDifficulty.Contains(TEXT("5-11-0")));
		TestEqual(TEXT("正の回数のキーは残る"), Result.Clears.ByDifficulty.Num(), 2);
	}

	// 固定は id 昇順に並べ直す
	{
		FCubelithSaveData Data = DataWithProgress();
		Data.Progress.Locks = {
			Lock(3, ECubelithSavedLockKind::Manual),
			Lock(0, ECubelithSavedLockKind::Hint),
			Lock(2, ECubelithSavedLockKind::Manual),
		};
		const FCubelithSaveData Result = Cubelith::SanitizeSaveData(Data);
		if (!TestEqual(TEXT("固定の数"), Result.Progress.Locks.Num(), 3))
		{
			return false;
		}
		TestEqual(TEXT("固定 0 番目"), Result.Progress.Locks[0].PieceId, 0);
		TestEqual(TEXT("固定 1 番目"), Result.Progress.Locks[1].PieceId, 2);
		TestEqual(TEXT("固定 2 番目"), Result.Progress.Locks[2].PieceId, 3);
		TestTrue(TEXT("種類は保つ"), Result.Progress.Locks[0].Kind == ECubelithSavedLockKind::Hint);
	}

	return true;
}

// 8. ロジックの型との相互変換（ELockKind と FPlacement）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSaveCoreConversionTest, "CUBELITH.Render.Save.CoreConversion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSaveCoreConversionTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SaveTestDetail;

	TestTrue(TEXT("Manual → ロジック"),
		Cubelith::ToCoreLockKind(ECubelithSavedLockKind::Manual) == Cubelith::ELockKind::Manual);
	TestTrue(TEXT("Hint → ロジック"),
		Cubelith::ToCoreLockKind(ECubelithSavedLockKind::Hint) == Cubelith::ELockKind::Hint);
	TestTrue(TEXT("Manual → 保存用"),
		Cubelith::ToSavedLockKind(Cubelith::ELockKind::Manual) == ECubelithSavedLockKind::Manual);
	TestTrue(TEXT("Hint → 保存用"),
		Cubelith::ToSavedLockKind(Cubelith::ELockKind::Hint) == ECubelithSavedLockKind::Hint);

	const FCubelithSavedPlacement Saved = Placement(2, 23, FIntVector(-4, 2, 7));
	const Cubelith::FPlacement Core = Cubelith::ToCorePlacement(Saved);
	TestEqual(TEXT("PieceId"), Core.PieceId, 2);
	TestEqual(TEXT("Orientation"), Core.Orientation, 23);
	TestTrue(TEXT("Position"), Core.Position == Cubelith::Vec3(-4, 2, 7));
	TestTrue(TEXT("往復して元に戻る"), Cubelith::ToSavedPlacement(Core) == Saved);

	return true;
}

#endif
