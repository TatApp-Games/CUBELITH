// タイトル / 難易度選択画面（RULES.md 6 章）。空間サイズ N・ピース分割数 M・パズルの回転を選んで「開始」。
// シードは小さく表示するだけで、この画面では変えない（タイトルへ来るたびに引き直す。RULES.md 2 章）。
// 移植元は Web 版の WebMock/src/ui/titleScreen.ts。
//
// 「続きから」とクリア回数の表示（RULES.md 6 章の残り）は後続タスクで足す。

#pragma once

#include "CoreMinimal.h"

#include "CubelithScreenWidget.h"

#include "CubelithTitleWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;

/**
 * 「開始」で確定する条件（titleScreen.ts の TitleSelection）。
 * USTRUCT にはしない（Blueprint へ出す必要が無く、`.uasset` も作らないため。ECubelithDragMode と同じ扱い）
 */
struct FCubelithTitleSelection
{
	/** 空間サイズ N（RULES.md 3.1。3..7） */
	int32 SpaceSize = 3;

	/** ピース分割数 M（N ごとの 5 段のプリセットのどれか） */
	int32 PieceCount = 4;

	/** パズルの回転 */
	bool bAllowRotation = false;

	/** この画面に出していたシード。押した時点の表示のまま渡す */
	uint32 Seed = 0;
};

/** 「開始」が押されたときに呼ぶ（titleScreen.ts の onStart）。ACubelithGameMode が受ける */
DECLARE_DELEGATE_OneParam(FCubelithTitleStarted, const FCubelithTitleSelection&);

/**
 * タイトル / 難易度選択画面。
 *
 * 人が UMG のウィジェットブループリントを作るときは、このクラス（か派生クラス）を親にして
 * 下の BindWidgetOptional と同じ名前の部品を置く（手順は Docs/SPEC_UE.md 4 章）。
 * N / M / 回転の選択肢のボタンは数が N で変わるので、人が置いた入れ物（`SpaceSizeRow` など）へ
 * C++ が並べる。入れ物が無ければ仮のレイアウトごと C++ で組む。
 */
UCLASS()
class CUBELITH_API UCubelithTitleWidget : public UCubelithScreenWidget
{
	GENERATED_BODY()

public:
	/**
	 * 初期選択とシードを渡す（AddToViewport の前に呼ぶ）。
	 * M が N のプリセットに無ければ最も近い段へ寄せる（Cubelith::NearestPreset）
	 */
	void SetInitialSelection(int32 N, int32 M, bool bInAllowRotation, uint32 InSeed);

	/** 「開始」が押されたとき */
	FCubelithTitleStarted OnStart;

protected:
	virtual void BuildFallbackLayout() override;
	virtual void BindBehavior() override;

	/** 画面の題（既定は "CUBELITH"） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	/** 題の下の 1 行（何をする画面か） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LeadText;

	/** 空間サイズ N のボタンを並べる入れ物（横並び） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> SpaceSizeRow;

	/** ピース分割数 M のボタンを並べる入れ物（N を変えると中身を作り直す） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> PieceCountRow;

	/** パズルの回転（なし / あり）のボタンを並べる入れ物 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> RotationRow;

	/** 選んでいる条件のまとめ（`N = 3 / M = 4 / 回転なし（27 ボクセルを 4 個に分割）`） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SummaryText;

	/** 「開始」 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> StartButton;

	/** シードの表示（小さく。RULES.md 6 章） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SeedText;

private:
	/** N のボタンを入れ物に並べる（選べる N は 3..7 で固定なので 1 度だけ） */
	void BuildSpaceSizeOptions();

	/** M のボタンを入れ物に並べ直す（N が変わるとプリセットが変わる。titleScreen.ts の rebuildPieceOptions） */
	void RebuildPieceCountOptions();

	/** 回転（なし / あり）のボタンを入れ物に並べる */
	void BuildRotationOptions();

	/** 選択状態と文字の表示を今の選択に合わせる（titleScreen.ts の syncSelection） */
	void SyncSelection();

	/** 「開始」を押したときの処理 */
	void HandleStartClicked();

	/** 選んでいる空間サイズ N */
	int32 SpaceSize = 3;

	/** 選んでいるピース分割数 M */
	int32 PieceCount = 4;

	/** 選んでいるパズルの回転 */
	bool bAllowRotation = false;

	/** 表示しているシード（「開始」でそのまま渡す） */
	uint32 Seed = 0;

	/**
	 * 選択肢のボタンを並べ終えたか。SetInitialSelection が「その場で反映する」か
	 * 「値を覚えておいて BindBehavior に任せる」かを分けるために持つ（呼ばれる順が経路で変わる）
	 */
	bool bOptionsBuilt = false;

	/** N のボタン（N → ボタン）。選択の色を付け替えるために持つ */
	UPROPERTY()
	TMap<int32, TObjectPtr<UButton>> SpaceSizeButtons;

	/** M のボタン（M → ボタン）。N を変えると作り直す */
	UPROPERTY()
	TMap<int32, TObjectPtr<UButton>> PieceCountButtons;

	/** 回転のボタン（index 0 = なし、1 = あり） */
	UPROPERTY()
	TObjectPtr<UButton> RotationOffButton;

	UPROPERTY()
	TObjectPtr<UButton> RotationOnButton;
};
