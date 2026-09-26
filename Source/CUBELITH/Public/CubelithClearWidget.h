// クリア画面（RULES.md 2 章 5 / 6 章）。「もう一度」（難易度はそのままシードだけ引き直して作り直す）と
// 「難易度を変える」（タイトルへ戻る）の 2 つだけを持つ。移植元は Web 版の WebMock/src/ui/clearScreen.ts。
//
// クリア演出（発光・融合・パーティクル・カメラの自動旋回。RULES.md 5.2）は U5 なので、この画面は持たない。
// ピースの操作を止めるのは ACubelithGameMode（ACubelithPlayerController::SetPieceInputEnabled）で、
// この画面は「押された」ことを返すだけ（Docs/SPEC_UE.md 4 章の「画面（UI）」）。

#pragma once

#include "CoreMinimal.h"

#include "CubelithScreenWidget.h"

#include "CubelithClearWidget.generated.h"

class UButton;
class UTextBlock;

/** クリア画面のボタンが押されたときに呼ぶ（clearScreen.ts の onRetry / onBackToTitle）。ACubelithGameMode が受ける */
DECLARE_DELEGATE(FCubelithClearAction);

/**
 * クリア画面。
 *
 * 人が UMG のウィジェットブループリントを作るときは、このクラス（か派生クラス）を親にして
 * 下の BindWidgetOptional と同じ名前・代入できる型の部品を置く（手順は Docs/SPEC_UE.md 4 章）。
 * 置かなかった部品はその表示が出ないだけで、他の部品は動く。
 */
UCLASS()
class CUBELITH_API UCubelithClearWidget : public UCubelithScreenWidget
{
	GENERATED_BODY()

public:
	/**
	 * 遊んだ盤面の条件を渡す（AddToViewport の前に呼ぶ）。丸めや寄せはしない
	 * （渡すのは実際に遊んだ盤面の値で、ここでは出すだけ）
	 */
	void SetBoardSummary(int32 N, int32 M, bool bInAllowRotation, uint32 InSeed);

	/** 「もう一度」が押された（難易度はそのままシードだけ引き直す。RULES.md 2 章 5） */
	FCubelithClearAction OnRetry;

	/** 「難易度を変える」が押された（タイトルへ戻る。RULES.md 2 章） */
	FCubelithClearAction OnBackToTitle;

protected:
	virtual void BuildFallbackLayout() override;
	virtual void BindBehavior() override;

	/** クリアの見出し（既定は "CLEAR"。clearScreen.ts の #clear-banner） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BannerText;

	/** 遊んだ盤面の条件（`N = 3 / M = 4 / 回転なし を組み上げた`） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SummaryText;

	/** 「もう一度」 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> RetryButton;

	/** 「難易度を変える」 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BackToTitleButton;

	/** シードの表示（小さく。タイトルと同じ形） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SeedText;

private:
	/** 今持っている条件を部品へ流す（部品が無ければ何もしない） */
	void Refresh();

	/** 遊んだ盤面の空間サイズ N */
	int32 SpaceSize = 0;

	/** 遊んだ盤面のピース分割数 M */
	int32 PieceCount = 0;

	/** 遊んだ盤面のパズルの回転 */
	bool bAllowRotation = false;

	/** 遊んだ盤面のシード */
	uint32 Seed = 0;
};
