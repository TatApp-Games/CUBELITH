// タイトル / 難易度選択画面（RULES.md 6 章）。空間サイズ N・ピース分割数 M・パズルの回転を選んで「開始」。
// シードは小さく表示するだけで、この画面では変えない（タイトルへ来るたびに引き直す。RULES.md 2 章）。
// 移植元は Web 版の WebMock/src/ui/titleScreen.ts。
//
// 途中の盤面（RULES.md 3.8）があれば「開始」の上に「続きから」を出し、その盤面の難易度と残りピース数を
// 添える。クリア回数は「合計」と「選んでいる難易度の回数」で 1 行出す（RULES.md 6 章）。文言を作るのは
// 純粋関数（CubelithTitleState.h）で、この画面はその結果を部品へ流すだけにしてある。

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

#include "CubelithSave.h"
#include "CubelithScreenWidget.h"
#include "CubelithTitleState.h"

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
 * 「続きから」が押されたときに呼ぶ（titleScreen.ts の onResume）。ACubelithGameMode が受ける。
 * 復元する盤面（難易度とシード）はセーブが持っているので、この画面は押されたことだけを返す
 */
DECLARE_DELEGATE(FCubelithTitleResumed);

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

	/**
	 * 途中の盤面（RULES.md 3.8）を渡す（AddToViewport の前に呼ぶ）。
	 * 未設定なら「続きから」とその 1 行を出さない（RULES.md 6 章の「無ければ出さない」）
	 */
	void SetResume(const TOptional<Cubelith::FTitleResume>& InResume);

	/**
	 * クリア回数（RULES.md 3.8）を渡す（AddToViewport の前に呼ぶ）。
	 * 「合計」はそのまま、「この難易度」は**今選んでいる難易度**の回数なので、選択を変えるたびに引き直す
	 */
	void SetClearCounts(const FCubelithSavedClears& InClears);

	/** 「開始」が押されたとき */
	FCubelithTitleStarted OnStart;

	/** 「続きから」が押されたとき（途中の盤面が無いときは出さないので呼ばれない） */
	FCubelithTitleResumed OnResume;

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

	/** 「続きから」（RULES.md 6 章。途中の盤面が無ければ隠す） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ResumeButton;

	/** 「続きから」に添える 1 行（`N = 3 / M = 4 / 回転なし・残り 2 ピース`。無ければ隠す） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResumeNoteText;

	/** 「開始」 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> StartButton;

	/** クリア回数の 1 行（`クリア 合計 3 回 / この難易度 1 回`。RULES.md 6 章） */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ClearCountText;

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

	/** 「続きから」を出す / 隠すと、添える 1 行を今の Resume に合わせる */
	void SyncResume();

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

	/** 途中の盤面（RULES.md 3.8）。未設定なら「続きから」を出さない */
	TOptional<Cubelith::FTitleResume> Resume;

	/** クリア回数（RULES.md 3.8）。UPROPERTY にしないのは UObject を指さない素の値だけだから */
	FCubelithSavedClears Clears;

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
