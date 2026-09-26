// プレイ中 HUD の表示を決める純粋な判断の実装。移植元は WebMock/src/ui/hud.ts の render()。

#include "CubelithHudState.h"

namespace Cubelith
{
	const TCHAR* const HudRotateModeLabelOff = TEXT("回転");
	const TCHAR* const HudRotateModeLabelOn = TEXT("回転解除");
	const TCHAR* const HudLockLabelLock = TEXT("固定");
	const TCHAR* const HudLockLabelUnlock = TEXT("固定解除");

	FHudDisplay ResolveHudDisplay(const FHudState& State)
	{
		FHudDisplay Display;

		const bool bHasSelection = State.SelectedPieceId != INDEX_NONE;
		// 選択が無いときの固定は見ない（呼び出し側が消し忘れても表示がねじれないように）
		const TOptional<ELockKind> LockKind = bHasSelection ? State.LockKind : TOptional<ELockKind>();
		const bool bLocked = LockKind.IsSet();
		const bool bHintLocked = bLocked && LockKind.GetValue() == ELockKind::Hint;

		// ピースに紐づく操作は選択しているときだけ出す（RULES.md 6 章）
		Display.bShowPieceControls = bHasSelection;

		// 回転モードのトグルはパズルの回転「あり」のときだけ出す（RULES.md 6 章）
		Display.bShowRotateMode = State.bAllowRotation;

		// 固定中のピースを選んでいる間は押せない（RULES.md 6 章）
		Display.bRotateModeEnabled = !bLocked;

		// 「効いている」のは回転「あり」の盤面で選択があり、そのピースが固定されていないときだけ
		// （hud.ts の rotating）。入力側は固定したピースでもモードのフラグを持ったままにできるので、
		// 見せ方はここで落とす。回転「なし」はトグルごと出ないので、そもそも効いた状態にならない
		Display.bRotateModeActive =
			State.bRotateModeOn && State.bAllowRotation && bHasSelection && !bLocked;

		// ヒントで固定したピースは解除できない（RULES.md 3.3 / 6 章）
		Display.bLockEnabled = !bHintLocked;
		Display.bLockActive = bLocked && LockKind.GetValue() == ELockKind::Manual;

		Display.bHintEnabled = State.bHintAvailable;

		Display.RotateModeLabel = Display.bRotateModeActive ? HudRotateModeLabelOn : HudRotateModeLabelOff;
		Display.LockLabel = bLocked ? HudLockLabelUnlock : HudLockLabelLock;

		if (!bHasSelection)
		{
			Display.StatusText = TEXT("ピースをクリック / タップして選択すると操作ボタンが出る");
			return Display;
		}

		// 解釈: hud.ts は固定中の案内を手動 / ヒントで分けず、「解除できない」は
		// ボタンの title 属性（マウスを乗せたときの吹き出し）で伝えている。UMG の仮の画面に
		// 吹き出しは無いので、ヒントの固定だけ案内の文を分けて「押せない理由」が読めるようにする
		if (bHintLocked)
		{
			Display.StatusText = FString::Printf(
				TEXT("選択中: ピース #%d（ヒントで固定 — 解除できない）"), State.SelectedPieceId);
			return Display;
		}

		if (bLocked)
		{
			Display.StatusText = FString::Printf(
				TEXT("選択中: ピース #%d（固定中 — 動かすには「固定解除」を押す）"), State.SelectedPieceId);
			return Display;
		}

		if (Display.bRotateModeActive)
		{
			Display.StatusText = FString::Printf(
				TEXT("選択中: ピース #%d（ドラッグで回転 / 離すと 90 度にスナップ）"), State.SelectedPieceId);
			return Display;
		}

		Display.StatusText = State.bAllowRotation
			? FString::Printf(
				TEXT("選択中: ピース #%d（ドラッグで移動 / 2 本指スワイプ・ひねりで回転 / ピンチでズーム）"),
				State.SelectedPieceId)
			: FString::Printf(
				TEXT("選択中: ピース #%d（ドラッグで移動 / ピンチでズーム）"), State.SelectedPieceId);
		return Display;
	}

	FString HudRemainingText(int32 Remaining, int32 Total)
	{
		return FString::Printf(TEXT("残り %d / %d"), Remaining, Total);
	}
}
