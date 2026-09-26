// クリア画面の実装（RULES.md 2 章 5 / 6 章）。移植元は WebMock/src/ui/clearScreen.ts。
// DOM と CSS の代わりに UWidgetTree で UMG の部品を組む（作りはタイトル / HUD と同じ）。

#include "CubelithClearWidget.h"

#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

#include "CubelithLog.h"

namespace
{
	/** 仮の画面の文字の大きさ（端末非依存。人が UMG で作り直す前提なので細かく詰めない） */
	constexpr int32 BannerFontSize = 32;
	constexpr int32 BodyFontSize = 16;
	constexpr int32 SeedFontSize = 12;

	/** 「もう一度」「難易度を変える」の横幅（長いラベルでも折り返さない程度に取る） */
	constexpr float ActionButtonWidthPx = 160.0f;

	/** パズルの回転の表示（titleScreen.ts / UCubelithTitleWidget と同じ文言） */
	const TCHAR* RotationLabel(bool bAllowRotation)
	{
		return bAllowRotation ? TEXT("回転あり") : TEXT("回転なし");
	}
}

void UCubelithClearWidget::SetBoardSummary(int32 N, int32 M, bool bInAllowRotation, uint32 InSeed)
{
	SpaceSize = N;
	PieceCount = M;
	bAllowRotation = bInAllowRotation;
	Seed = InSeed;

	// 先に画面が組まれていれば（= 普通の経路）その場で反映する。まだなら BindBehavior がこの値から出す
	Refresh();
}

void UCubelithClearWidget::BuildFallbackLayout()
{
	// 解釈: パネルは画面下部へ寄せる（clearScreen.ts が screen--bottom を使うのと同じ理由 ＝
	// 組み上がった立方体を隠さないため）。中央寄せにはしない
	UVerticalBox* const Stack = ConstructBottomPanelRoot();
	if (Stack == nullptr)
	{
		return;
	}

	BannerText = ConstructText(Stack, FText::FromString(TEXT("CLEAR")), BannerFontSize);
	// 中身は Refresh が入れる（人のレイアウトでも同じ道を通す）ので、ここでは空のまま置く
	SummaryText = ConstructText(Stack, FText::GetEmpty(), BodyFontSize);

	UHorizontalBox* const ActionRow = ConstructRow(Stack);
	// 押したときの処理は BindBehavior でまとめて結ぶ（人の UMG のボタンと同じ道を通す）ので、ここでは付けない
	RetryButton = ConstructButton(ActionRow,
		FText::FromString(TEXT("もう一度")), TFunction<void()>(), ActionButtonWidthPx);
	BackToTitleButton = ConstructButton(ActionRow,
		FText::FromString(TEXT("難易度を変える")), TFunction<void()>(), ActionButtonWidthPx);

	SeedText = ConstructText(Stack, FText::GetEmpty(), SeedFontSize);
}

void UCubelithClearWidget::BindBehavior()
{
	// 盤面を触るのは ACubelithGameMode で、ここは「押された」ことだけを返す
	BindButton(RetryButton, [this]() { OnRetry.ExecuteIfBound(); });
	BindButton(BackToTitleButton, [this]() { OnBackToTitle.ExecuteIfBound(); });

	Refresh();

	if (RetryButton == nullptr && BackToTitleButton == nullptr)
	{
		// どちらも無いとクリアした盤面から出られないので、気付けるように出す（仮の画面では必ずある）
		UE_LOG(LogCubelith, Warning,
			TEXT("クリア画面に RetryButton も BackToTitleButton も無いのでクリアした盤面から出られない")
			TEXT("（UMG に同じ名前の UButton を置く）"));
	}
}

void UCubelithClearWidget::Refresh()
{
	// 解釈: 出すのは N / M / パズルの回転 / シード。clearScreen.ts は N / M とシードだけだが、
	// 同じ盤面を開き直せるようにパズルの回転も添える（タイトルのまとめの行と同じ文言）
	SetTextSafe(SummaryText, FText::FromString(FString::Printf(
		TEXT("N = %d / M = %d / %s を組み上げた"), SpaceSize, PieceCount, RotationLabel(bAllowRotation))));

	SetTextSafe(SeedText, FText::FromString(FString::Printf(TEXT("seed %u"), Seed)));
}
