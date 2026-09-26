// CubelithHudState（プレイ中 HUD の「出す / 押せる / ラベル」の判断）のテスト。
// 移植元 WebMock/src/ui/hud.ts に対応するテストは WebMock にも無いので、確かめるのは
// RULES.md 6 章の表示の条件そのもの（選択しているときだけ出す・回転「あり」のときだけ出す・
// 固定中は回転モードに入れない・ヒントの固定は解除できない・ヒントが使えないときは押せない）と、
// 残りピース数の文字。
// ウィジェットもアクタも立てない（Scripts/Test.ps1 は -nullrhi で回る）。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithHudState.h"
#include "Game.h"

namespace CubelithRenderTests
{
	// HudStateTest.cpp 専用のヘルパ。unity ビルドでは他のテストと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace HudStateTestDetail
	{
		/** ピースを 1 つ選んでいて固定は無い、回転「あり」の盤面（いちばん普通の状態） */
		Cubelith::FHudState Selected(int32 PieceId = 2)
		{
			Cubelith::FHudState State;
			State.SelectedPieceId = PieceId;
			State.bAllowRotation = true;
			State.bHintAvailable = true;
			return State;
		}
	}
}

// 1. 選択していない間は、ピースに紐づく操作の行を出さない（RULES.md 6 章）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHudStateNoSelectionTest, "CUBELITH.Render.HudState.NoSelection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithHudStateNoSelectionTest::RunTest(const FString& Parameters)
{
	Cubelith::FHudState State;
	State.bAllowRotation = true;
	State.bHintAvailable = true;

	const Cubelith::FHudDisplay Display = Cubelith::ResolveHudDisplay(State);

	TestFalse(TEXT("ピースに紐づく操作の行は出さない"), Display.bShowPieceControls);
	TestFalse(TEXT("回転モードは効いていない"), Display.bRotateModeActive);
	TestEqual(TEXT("回転モードのラベルは「回転」"), Display.RotateModeLabel, FString(TEXT("回転")));
	TestEqual(TEXT("固定のラベルは「固定」"), Display.LockLabel, FString(TEXT("固定")));
	TestTrue(TEXT("ヒントは選択と関係なく押せる"), Display.bHintEnabled);
	TestEqual(TEXT("案内は選択を促す"), Display.StatusText,
		FString(TEXT("ピースをクリック / タップして選択すると操作ボタンが出る")));

	// 選択が無いのに固定が残っていても、表示はねじれない
	State.LockKind = Cubelith::ELockKind::Manual;
	const Cubelith::FHudDisplay Stale = Cubelith::ResolveHudDisplay(State);
	TestEqual(TEXT("選択が無ければ固定は見ない"), Stale.LockLabel, FString(TEXT("固定")));
	TestFalse(TEXT("選択が無ければ固定は選ばれていない"), Stale.bLockActive);

	return true;
}

// 2. 回転モードのトグルはパズルの回転「あり」のときだけ出す（RULES.md 6 章）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHudStateRotationOptionTest, "CUBELITH.Render.HudState.RotationOption",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithHudStateRotationOptionTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::HudStateTestDetail;

	const Cubelith::FHudDisplay WithRotation = Cubelith::ResolveHudDisplay(Selected());
	TestTrue(TEXT("回転「あり」なら出す"), WithRotation.bShowRotateMode);
	TestEqual(TEXT("回転「あり」の案内"), WithRotation.StatusText,
		FString(TEXT("選択中: ピース #2（ドラッグで移動 / 2 本指スワイプ・ひねりで回転 / ピンチでズーム）")));

	Cubelith::FHudState NoRotation = Selected();
	NoRotation.bAllowRotation = false;
	const Cubelith::FHudDisplay Display = Cubelith::ResolveHudDisplay(NoRotation);

	TestFalse(TEXT("回転「なし」なら出さない"), Display.bShowRotateMode);
	TestTrue(TEXT("「固定 / 固定解除」は回転と関係なく出す"), Display.bShowPieceControls);
	TestEqual(TEXT("回転「なし」の案内"), Display.StatusText,
		FString(TEXT("選択中: ピース #2（ドラッグで移動 / ピンチでズーム）")));

	return true;
}

// 3. 回転モードに入っている間はラベルが「回転解除」になり、案内も変わる（RULES.md 6 章）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHudStateRotateModeTest, "CUBELITH.Render.HudState.RotateMode",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithHudStateRotateModeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::HudStateTestDetail;

	Cubelith::FHudState State = Selected();
	State.bRotateModeOn = true;
	const Cubelith::FHudDisplay Display = Cubelith::ResolveHudDisplay(State);

	TestTrue(TEXT("回転モードが効いている"), Display.bRotateModeActive);
	TestTrue(TEXT("押せる"), Display.bRotateModeEnabled);
	TestEqual(TEXT("ラベルは「回転解除」"), Display.RotateModeLabel, FString(TEXT("回転解除")));
	TestEqual(TEXT("回転モード中の案内"), Display.StatusText,
		FString(TEXT("選択中: ピース #2（ドラッグで回転 / 離すと 90 度にスナップ）")));

	// 回転「なし」の盤面では、フラグが立っていても効かない（入力側も入れない）
	Cubelith::FHudState NoRotation = State;
	NoRotation.bAllowRotation = false;
	TestFalse(TEXT("回転「なし」では効かない"), Cubelith::ResolveHudDisplay(NoRotation).bRotateModeActive);

	return true;
}

// 4. 手動の固定中は回転モードのトグルを押せず、ラベルが「固定解除」になる（RULES.md 6 章）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHudStateManualLockTest, "CUBELITH.Render.HudState.ManualLock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithHudStateManualLockTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::HudStateTestDetail;

	Cubelith::FHudState State = Selected();
	State.LockKind = Cubelith::ELockKind::Manual;
	// 固定する前に回転モードに入っていた（入力側はフラグを持ったままでも、表示では効かせない）
	State.bRotateModeOn = true;

	const Cubelith::FHudDisplay Display = Cubelith::ResolveHudDisplay(State);

	TestTrue(TEXT("行は出す"), Display.bShowPieceControls);
	TestTrue(TEXT("回転モードのトグルは出す"), Display.bShowRotateMode);
	TestFalse(TEXT("固定中は回転モードのトグルを押せない"), Display.bRotateModeEnabled);
	TestFalse(TEXT("固定中は回転モードが効かない"), Display.bRotateModeActive);
	TestEqual(TEXT("ラベルは「回転」に戻る"), Display.RotateModeLabel, FString(TEXT("回転")));

	TestTrue(TEXT("手動の固定は解除できる"), Display.bLockEnabled);
	TestTrue(TEXT("手動の固定は選ばれている見せ方"), Display.bLockActive);
	TestEqual(TEXT("ラベルは「固定解除」"), Display.LockLabel, FString(TEXT("固定解除")));
	TestEqual(TEXT("固定中の案内"), Display.StatusText,
		FString(TEXT("選択中: ピース #2（固定中 — 動かすには「固定解除」を押す）")));

	return true;
}

// 5. ヒントで固定したピースは「固定解除」を押せない（RULES.md 3.3 / 6 章）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHudStateHintLockTest, "CUBELITH.Render.HudState.HintLock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithHudStateHintLockTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::HudStateTestDetail;

	Cubelith::FHudState State = Selected();
	State.LockKind = Cubelith::ELockKind::Hint;

	const Cubelith::FHudDisplay Display = Cubelith::ResolveHudDisplay(State);

	TestFalse(TEXT("ヒントの固定は解除できない"), Display.bLockEnabled);
	TestFalse(TEXT("ヒントの固定は「選ばれている」にはしない"), Display.bLockActive);
	TestEqual(TEXT("ラベルは「固定解除」（押せないだけ）"), Display.LockLabel, FString(TEXT("固定解除")));
	TestFalse(TEXT("固定中なので回転モードのトグルも押せない"), Display.bRotateModeEnabled);
	TestEqual(TEXT("解除できないことを案内に出す"), Display.StatusText,
		FString(TEXT("選択中: ピース #2（ヒントで固定 — 解除できない）")));

	return true;
}

// 6. ヒントが使えないときは「ヒント」を押せない（RULES.md 3.7）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHudStateHintEnabledTest, "CUBELITH.Render.HudState.HintEnabled",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithHudStateHintEnabledTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::HudStateTestDetail;

	Cubelith::FHudState State = Selected();
	State.bHintAvailable = false;

	TestFalse(TEXT("使えなければ押せない"), Cubelith::ResolveHudDisplay(State).bHintEnabled);

	State.bHintAvailable = true;
	TestTrue(TEXT("使えれば押せる"), Cubelith::ResolveHudDisplay(State).bHintEnabled);

	return true;
}

// 7. 残りピース数の文字（RULES.md 6 章の「残りピース数（未確定のピース数）」）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithHudStateRemainingTextTest, "CUBELITH.Render.HudState.RemainingText",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithHudStateRemainingTextTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("残り 3 / 5"), Cubelith::HudRemainingText(3, 5), FString(TEXT("残り 3 / 5")));
	// クリアすると 0 になる（UnsettledPieceCount の数え方は CubelithProgress.h）
	TestEqual(TEXT("残り 0 / 12"), Cubelith::HudRemainingText(0, 12), FString(TEXT("残り 0 / 12")));

	return true;
}

#endif
