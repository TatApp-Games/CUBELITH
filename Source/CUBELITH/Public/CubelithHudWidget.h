// プレイ中 HUD（RULES.md 6 章）。残りピース数・回転モードのトグル・「固定 / 固定解除」・「散らし直す」・
// 「ヒント」・「次の問題」・「難易度へ戻る」を持つ。移植元は Web 版の WebMock/src/ui/hud.ts と、
// それを繋いでいる WebMock/src/main.ts の HUD のコールバック。
//
// 縦持ちの画面を前提に、仮のレイアウトは画面下部へ寄せる（RULES.md 6 章。ボタンの大きさは
// UCubelithScreenWidget::ConstructButton が 44 px 以上を確保する）。
//
// 表示の条件（出す / 押せる / ラベル）は Cubelith::ResolveHudDisplay（CubelithHudState.h）が決め、
// このクラスはその結果を UMG の部品へ流すだけにしてある（判断はテストできるところへ置く）。
//
// 状態を渡す口は hud.ts と同じ 5 つ（SetSelected / SetRemaining / SetLock / SetRotateMode /
// SetHintEnabled）+ 難易度で変わる SetAllowRotation。押されたことは下のデリゲートで
// ACubelithGameMode へ返す（画面は値を持つだけで、盤面を触るのは GameMode。Docs/SPEC_UE.md 4 章）。

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

#include "CubelithHudState.h"
#include "CubelithScreenWidget.h"
#include "Game.h"

#include "CubelithHudWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;

/** HUD のボタンが押されたときに呼ぶ（hud.ts の HudCallbacks の 1 つ分）。ACubelithGameMode が受ける */
DECLARE_DELEGATE(FCubelithHudAction);

/**
 * プレイ中の HUD。
 *
 * 人が UMG のウィジェットブループリントを作るときは、このクラス（か派生クラス）を親にして
 * 下の BindWidgetOptional と同じ名前・代入できる型の部品を置く（手順は Docs/SPEC_UE.md 4 章）。
 * 置かなかった部品はその表示が出ないだけで、他の部品は動く。
 */
UCLASS()
class CUBELITH_API UCubelithHudWidget : public UCubelithScreenWidget
{
	GENERATED_BODY()

public:
	/**
	 * この盤面がパズルの回転「あり」か（RULES.md 3.1）を渡す（hud.ts の HudOptions.allowRotation）。
	 * 「なし」なら回転モードのトグルを出さない（RULES.md 6 章）
	 */
	void SetAllowRotation(bool bInAllowRotation);

	/** 選択中のピースを渡す（INDEX_NONE で未選択）。未選択ならピースに紐づく操作の行を隠す */
	void SetSelected(int32 PieceId);

	/** 残りピース数（RULES.md 6 章。未確定のピース数 / 全体） */
	void SetRemaining(int32 Remaining, int32 Total);

	/**
	 * 選択中のピースの固定を渡す（Cubelith::FGame::LockKindOf の戻り値そのまま）。
	 * ラベル（固定 / 固定解除）と、回転モードのトグル・「固定解除」を押せるかが変わる
	 */
	void SetLock(const TOptional<Cubelith::ELockKind>& Kind);

	/**
	 * 回転モードのオン / オフを渡す。実際に入れたかを決めるのは入力側
	 * （ACubelithPlayerController::SetRotateMode）なので、その結果を渡す
	 */
	void SetRotateMode(bool bEnabled);

	/** 「ヒント」を押せるか（RULES.md 3.7。ACubelithGameMode::IsHintAvailable の結果） */
	void SetHintEnabled(bool bEnabled);

	/** 回転モードのトグルが押された（入れるか抜けるかは入力側が決める） */
	FCubelithHudAction OnToggleRotateMode;

	/** 「固定 / 固定解除」が押された */
	FCubelithHudAction OnToggleLock;

	/** 「散らし直す」が押された（RULES.md 3.3「やり直し」） */
	FCubelithHudAction OnScatterAgain;

	/** 「ヒント」が押された（RULES.md 3.7） */
	FCubelithHudAction OnHint;

	/** 「次の問題」が押された（難易度はそのままシードだけ引き直す。RULES.md 2 章） */
	FCubelithHudAction OnNext;

	/** 「難易度へ戻る」が押された（タイトルへ戻る。RULES.md 2 章） */
	FCubelithHudAction OnBackToTitle;

protected:
	virtual void BuildFallbackLayout() override;
	virtual void BindBehavior() override;

	/** 残りピース数（「残り 2 / 5」） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RemainingText;

	/** 今できる操作の案内（選択していないとき・固定中・回転モード中で文が変わる） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	/**
	 * ピースに紐づく操作（回転モードのトグルと「固定 / 固定解除」）の入れ物。
	 * ピースを選択していないときは行ごと隠す（RULES.md 6 章）
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> PieceControlsRow;

	/** 回転モードのトグル（「回転 / 回転解除」）。パズルの回転「なし」では隠す */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> RotateModeButton;

	/** 「固定 / 固定解除」 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> LockButton;

	/** 常に出しているボタンの入れ物（人のレイアウトでは使わなくてよい） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> FooterRow;

	/** 「難易度へ戻る」 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BackToTitleButton;

	/** 「散らし直す」 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ScatterAgainButton;

	/** 「ヒント」 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> HintButton;

	/** 「次の問題」 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> NextButton;

private:
	/** 今の状態（State と残りピース数）を部品へ流す（hud.ts の render） */
	void Refresh();

	/** 出す / 押せる / ラベルを決める材料（CubelithHudState.h） */
	Cubelith::FHudState State;

	/** 残りピース数の表示（未確定のピース数 / 全体） */
	int32 Remaining = 0;
	int32 Total = 0;
};
