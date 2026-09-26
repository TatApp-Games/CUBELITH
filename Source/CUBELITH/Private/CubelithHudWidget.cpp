// プレイ中 HUD の実装（RULES.md 6 章）。移植元は WebMock/src/ui/hud.ts。
// DOM と CSS の代わりに UWidgetTree で UMG の部品を組み、hud.ts の render() に当たる判断は
// Cubelith::ResolveHudDisplay（CubelithHudState.h）へ切り出してある。

#include "CubelithHudWidget.h"

#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

#include "CubelithLog.h"

namespace
{
	/** 仮の画面の文字の大きさ（端末非依存。人が UMG で作り直す前提なので細かく詰めない） */
	constexpr int32 RemainingFontSize = 20;
	constexpr int32 StatusFontSize = 12;

	/**
	 * ピースに紐づく操作のボタンの横幅（「回転解除」「固定解除」でラベルが伸びても幅が跳ねないように広めに取る）。
	 * 指で押せる高さは ConstructButton が確保する（RULES.md 6 章の 44 px 以上）
	 */
	constexpr float PieceButtonWidthPx = 96.0f;
}

void UCubelithHudWidget::SetAllowRotation(bool bInAllowRotation)
{
	State.bAllowRotation = bInAllowRotation;

	// 回転「なし」の盤面では回転モードそのものが無い（RULES.md 3.1）
	if (!bInAllowRotation)
	{
		State.bRotateModeOn = false;
	}

	Refresh();
}

void UCubelithHudWidget::SetSelected(int32 PieceId)
{
	if (State.SelectedPieceId == PieceId)
	{
		Refresh();
		return;
	}

	State.SelectedPieceId = PieceId;

	// 選択が変われば入力側も回転モードを抜ける（ACubelithPlayerController::SetSelectedPiece）ので表示も揃える。
	// 固定は続けて SetLock で渡される（hud.ts の setSelected と同じ順）
	State.bRotateModeOn = false;
	State.LockKind.Reset();

	Refresh();
}

void UCubelithHudWidget::SetRemaining(int32 InRemaining, int32 InTotal)
{
	Remaining = InRemaining;
	Total = InTotal;
	Refresh();
}

void UCubelithHudWidget::SetLock(const TOptional<Cubelith::ELockKind>& Kind)
{
	State.LockKind = Kind;
	Refresh();
}

void UCubelithHudWidget::SetRotateMode(bool bEnabled)
{
	// 回転「なし」の盤面では回転モードに入れない（hud.ts の setRotateMode と同じ）
	State.bRotateModeOn = bEnabled && State.bAllowRotation;
	Refresh();
}

void UCubelithHudWidget::SetHintEnabled(bool bEnabled)
{
	State.bHintAvailable = bEnabled;
	Refresh();
}

void UCubelithHudWidget::BuildFallbackLayout()
{
	// 縦持ちの画面で HUD は下部に寄せる（RULES.md 6 章）
	UVerticalBox* const Stack = ConstructBottomPanelRoot();
	if (Stack == nullptr)
	{
		return;
	}

	// 中身は Refresh が入れる（人のレイアウトでも同じ道を通す）ので、ここでは空のまま置く
	RemainingText = ConstructText(Stack, FText::GetEmpty(), RemainingFontSize);
	StatusText = ConstructText(Stack, FText::GetEmpty(), StatusFontSize);

	// ピースに紐づく操作（選択しているときだけ出す行）
	PieceControlsRow = ConstructRow(Stack);
	RotateModeButton = ConstructButton(PieceControlsRow,
		FText::FromString(Cubelith::HudRotateModeLabelOff), TFunction<void()>(), PieceButtonWidthPx);
	LockButton = ConstructButton(PieceControlsRow,
		FText::FromString(Cubelith::HudLockLabelLock), TFunction<void()>(), PieceButtonWidthPx);

	// 常に出している行（hud.ts の #hud-footer と同じ並び。「サウンド」は未実装なので置かない）
	FooterRow = ConstructRow(Stack);
	BackToTitleButton = ConstructButton(FooterRow, FText::FromString(TEXT("難易度へ戻る")), TFunction<void()>());
	ScatterAgainButton = ConstructButton(FooterRow, FText::FromString(TEXT("散らし直す")), TFunction<void()>());
	HintButton = ConstructButton(FooterRow, FText::FromString(TEXT("ヒント")), TFunction<void()>());
	NextButton = ConstructButton(FooterRow, FText::FromString(TEXT("次の問題")), TFunction<void()>());
}

void UCubelithHudWidget::BindBehavior()
{
	// 押したときの処理は仮の画面でも人のレイアウトでも同じ道を通す（UCubelithScreenWidget の設計）。
	// 盤面を触るのは ACubelithGameMode で、ここは「押された」ことだけを返す
	BindButton(RotateModeButton, [this]() { OnToggleRotateMode.ExecuteIfBound(); });
	BindButton(LockButton, [this]() { OnToggleLock.ExecuteIfBound(); });
	BindButton(BackToTitleButton, [this]() { OnBackToTitle.ExecuteIfBound(); });
	BindButton(ScatterAgainButton, [this]() { OnScatterAgain.ExecuteIfBound(); });
	BindButton(HintButton, [this]() { OnHint.ExecuteIfBound(); });
	BindButton(NextButton, [this]() { OnNext.ExecuteIfBound(); });

	if (BackToTitleButton == nullptr)
	{
		// これが無いと遊んでいる盤面から出られなくなるので、気付けるように出す（仮の画面には必ずある）
		UE_LOG(LogCubelith, Warning,
			TEXT("HUD に BackToTitleButton が無いのでタイトルへ戻れない（UMG に同じ名前の UButton を置く）"));
	}

	Refresh();
}

void UCubelithHudWidget::Refresh()
{
	// 出す / 押せる / ラベルの判断は純粋関数に任せ、ここは結果を部品へ流すだけ（CubelithHudState.h）
	const Cubelith::FHudDisplay Display = Cubelith::ResolveHudDisplay(State);

	SetTextSafe(RemainingText, FText::FromString(Cubelith::HudRemainingText(Remaining, Total)));
	SetTextSafe(StatusText, FText::FromString(Display.StatusText));

	// ピースを選択しているときだけ出す行（RULES.md 6 章）
	SetWidgetVisible(PieceControlsRow, Display.bShowPieceControls);

	// 回転モードのトグルはパズルの回転「あり」のときだけ出す（同じく RULES.md 6 章）。
	// 解釈: hud.ts は回転「なし」ならボタンを作らないが、人の UMG には置かれているかもしれないので、
	// UE 版は「作ってから隠す」で揃える（隠すと場所も取らないので見え方は同じ）
	SetButtonVisible(RotateModeButton, Display.bShowRotateMode);
	SetButtonLabel(RotateModeButton, FText::FromString(Display.RotateModeLabel));
	SetButtonSelected(RotateModeButton, Display.bRotateModeActive);
	if (RotateModeButton != nullptr)
	{
		// 固定中のピースを選んでいる間は押せない（RULES.md 6 章）
		RotateModeButton->SetIsEnabled(Display.bRotateModeEnabled);
	}

	SetButtonLabel(LockButton, FText::FromString(Display.LockLabel));
	SetButtonSelected(LockButton, Display.bLockActive);
	if (LockButton != nullptr)
	{
		// ヒントで固定したピースを選んでいる間は「固定解除」を押せない（RULES.md 6 章）
		LockButton->SetIsEnabled(Display.bLockEnabled);
	}

	if (HintButton != nullptr)
	{
		// 未固定のピースが 1 個以下では押せない（RULES.md 3.7）
		HintButton->SetIsEnabled(Display.bHintEnabled);
	}
}
