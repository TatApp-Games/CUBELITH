// タイトル / 難易度選択画面に出す文言の材料（CubelithTitleState.h。RULES.md 3.8 / 6 章）のテスト。
// 移植元 WebMock/src/ui/titleScreen.ts に対応するテストは WebMock にも無いので、確かめるのは
// 「続きから」に添える 1 行とクリア回数の 1 行・まとめの行の形と、保存された盤面からの取り出し方。
// ウィジェットは立てない（判断と文言を純粋関数へ切り出してあるので、値を渡すだけで確かめられる）。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithTitleState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTitleStateRotationLabelTest, "CUBELITH.Render.TitleState.RotationLabel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTitleStateRotationLabelTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("回転なし"), FString(Cubelith::RotationLabel(false)), TEXT("回転なし"));
	TestEqual(TEXT("回転あり"), FString(Cubelith::RotationLabel(true)), TEXT("回転あり"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTitleStateSummaryTest, "CUBELITH.Render.TitleState.Summary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTitleStateSummaryTest::RunTest(const FString& Parameters)
{
	// ボクセル数は N の 3 乗（titleScreen.ts の `spaceSize ** 3`）
	TestEqual(TEXT("既定の難易度"), Cubelith::TitleSummaryText(3, 4, false),
		TEXT("N = 3 / M = 4 / 回転なし（27 ボクセルを 4 個に分割）"));
	TestEqual(TEXT("回転あり"), Cubelith::TitleSummaryText(7, 27, true),
		TEXT("N = 7 / M = 27 / 回転あり（343 ボクセルを 27 個に分割）"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTitleStateResumeTest, "CUBELITH.Render.TitleState.Resume",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTitleStateResumeTest::RunTest(const FString& Parameters)
{
	// 保存された盤面から取り出すのは難易度と残りピース数だけ（配置と固定はタイトルでは使わない）
	FCubelithSavedProgress Progress;
	Progress.Difficulty.SpaceSize = 5;
	Progress.Difficulty.PieceCount = 11;
	Progress.Difficulty.bAllowRotation = true;
	Progress.Seed = 20260926;
	Progress.Remaining = 4;

	const Cubelith::FTitleResume Resume = Cubelith::MakeTitleResume(Progress);
	TestEqual(TEXT("N"), Resume.SpaceSize, 5);
	TestEqual(TEXT("M"), Resume.PieceCount, 11);
	TestTrue(TEXT("パズルの回転"), Resume.bAllowRotation);
	TestEqual(TEXT("残りピース数"), Resume.Remaining, 4);

	TestEqual(TEXT("添える 1 行"), Cubelith::TitleResumeText(Resume),
		TEXT("N = 5 / M = 11 / 回転あり・残り 4 ピース"));

	// 残り 0 ピース（クリア直前で中断した盤面）も出す。クリアした盤面はそもそも保存されない（RULES.md 3.8）
	Cubelith::FTitleResume Finished = Resume;
	Finished.Remaining = 0;
	Finished.bAllowRotation = false;
	TestEqual(TEXT("残り 0"), Cubelith::TitleResumeText(Finished),
		TEXT("N = 5 / M = 11 / 回転なし・残り 0 ピース"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTitleStateClearCountTest, "CUBELITH.Render.TitleState.ClearCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTitleStateClearCountTest::RunTest(const FString& Parameters)
{
	// RULES.md 6 章の「合計」と「選んでいる難易度の回数」を 1 行で
	TestEqual(TEXT("何も遊んでいない"), Cubelith::TitleClearCountText(0, 0),
		TEXT("クリア 合計 0 回 / この難易度 0 回"));
	TestEqual(TEXT("合計とこの難易度が違う"), Cubelith::TitleClearCountText(12, 3),
		TEXT("クリア 合計 12 回 / この難易度 3 回"));
	return true;
}

#endif
