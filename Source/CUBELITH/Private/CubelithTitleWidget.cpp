// タイトル / 難易度選択画面の実装（RULES.md 6 章）。移植元は WebMock/src/ui/titleScreen.ts。
// 選択肢のボタンは数が N で変わるので、人が UMG を作っても入れ物（Row）だけを借りて中身は C++ が並べる。

#include "CubelithTitleWidget.h"

#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

#include "CubelithDifficulty.h"
#include "CubelithLog.h"
#include "CubelithTitleState.h"
#include "Generate.h"

namespace
{
	/** 数値 1 つのボタンのラベル。桁区切りが入らないよう FText::AsNumber は使わない */
	FText NumberLabel(int32 Value)
	{
		return FText::FromString(FString::FromInt(Value));
	}

	/** 仮の画面の文字の大きさ（端末非依存。人が UMG で作り直す前提なので細かく詰めない） */
	constexpr int32 TitleFontSize = 40;
	constexpr int32 BodyFontSize = 16;
	constexpr int32 SeedFontSize = 12;

	/** 「開始」と「続きから」の横幅（指で押せる高さは ConstructButton が確保する） */
	constexpr float StartButtonWidthPx = 240.0f;
}

void UCubelithTitleWidget::SetInitialSelection(int32 N, int32 M, bool bInAllowRotation, uint32 InSeed)
{
	SpaceSize = FMath::Clamp(N, Cubelith::MinSpaceSize, Cubelith::MaxSpaceSize);
	PieceCount = M;
	bAllowRotation = bInAllowRotation;
	Seed = InSeed;

	// 先に画面が組まれていれば（= 普通の経路）その場で反映する。まだなら BindBehavior がこの値から組む
	if (bOptionsBuilt)
	{
		RebuildPieceCountOptions();
		SyncSelection();
	}
}

void UCubelithTitleWidget::SetResume(const TOptional<Cubelith::FTitleResume>& InResume)
{
	Resume = InResume;

	// SetInitialSelection と同じで、画面がまだ組まれていなければ BindBehavior が組み終わりに反映する
	if (bOptionsBuilt)
	{
		SyncResume();
	}
}

void UCubelithTitleWidget::SetClearCounts(const FCubelithSavedClears& InClears)
{
	Clears = InClears;

	// 「この難易度」の回数は選択で変わるので、文言を作るのは SyncSelection 側
	if (bOptionsBuilt)
	{
		SyncSelection();
	}
}

void UCubelithTitleWidget::BuildFallbackLayout()
{
	UVerticalBox* const Stack = ConstructCenteredPanelRoot();
	if (Stack == nullptr)
	{
		return;
	}

	TitleText = ConstructText(Stack, FText::FromString(TEXT("CUBELITH")), TitleFontSize);
	LeadText = ConstructText(Stack,
		FText::FromString(TEXT("空間サイズ・分割数・パズルの回転を選んで開始する")), BodyFontSize);

	ConstructText(Stack, FText::FromString(TEXT("空間サイズ N")), BodyFontSize);
	SpaceSizeRow = ConstructRow(Stack);

	ConstructText(Stack, FText::FromString(TEXT("分割数 M")), BodyFontSize);
	PieceCountRow = ConstructRow(Stack);

	ConstructText(Stack, FText::FromString(TEXT("パズルの回転")), BodyFontSize);
	RotationRow = ConstructRow(Stack);

	SummaryText = ConstructText(Stack, FText::GetEmpty(), BodyFontSize);

	// 解釈: 「続きから」は「開始」の上に置き、下に難易度と残りピース数を 1 行添える（RULES.md 6 章の
	// 「『開始』の上に『続きから』を出し、その盤面の難易度と残りピース数を添える」。titleScreen.ts と同じ並び）。
	// 途中の盤面が無いときは SyncResume が行ごと隠す（ボタンを作らないのではなく隠すのは、人の UMG に
	// 置かれている場合と同じ経路にするため。HUD の「解釈:」と同じ考え方）
	ResumeButton = ConstructButton(Stack, FText::FromString(TEXT("続きから")), TFunction<void()>(), StartButtonWidthPx);
	ResumeNoteText = ConstructText(Stack, FText::GetEmpty(), BodyFontSize);

	// 押したときの処理は BindBehavior でまとめて結ぶ（人の UMG のボタンと同じ道を通す）ので、ここでは付けない
	StartButton = ConstructButton(Stack, FText::FromString(TEXT("開始")), TFunction<void()>(), StartButtonWidthPx);

	ClearCountText = ConstructText(Stack, FText::GetEmpty(), BodyFontSize);

	SeedText = ConstructText(Stack, FText::GetEmpty(), SeedFontSize);
}

void UCubelithTitleWidget::BindBehavior()
{
	BindButton(StartButton, [this]() { HandleStartClicked(); });
	// 復元する盤面はセーブが持っているので、この画面は押されたことだけを返す（RULES.md 3.8）
	BindButton(ResumeButton, [this]() { OnResume.ExecuteIfBound(); });

	BuildSpaceSizeOptions();
	BuildRotationOptions();
	RebuildPieceCountOptions();

	bOptionsBuilt = true;
	SyncSelection();
	SyncResume();

	if (StartButton == nullptr)
	{
		// 人の UMG に「開始」が無いと先へ進めないので、気付けるように出す（仮の画面では必ずある）
		UE_LOG(LogCubelith, Warning,
			TEXT("タイトル画面に StartButton が無いのでパズルを開始できない（UMG に同じ名前の UButton を置く）"));
	}
}

void UCubelithTitleWidget::BuildSpaceSizeOptions()
{
	SpaceSizeButtons.Reset();
	if (SpaceSizeRow == nullptr)
	{
		return;
	}
	SpaceSizeRow->ClearChildren();

	// 選べる N は 3..7 で固定（RULES.md 3.1。difficulty.ts の spaceSizes）
	for (int32 Value = Cubelith::MinSpaceSize; Value <= Cubelith::MaxSpaceSize; ++Value)
	{
		UButton* const Button = ConstructButton(SpaceSizeRow, NumberLabel(Value), [this, Value]()
		{
			if (SpaceSize == Value)
			{
				return;
			}
			SpaceSize = Value;
			// N が変わると M の有効範囲が変わるので、プリセットのボタンごと作り直す
			RebuildPieceCountOptions();
			SyncSelection();
		});
		SpaceSizeButtons.Add(Value, Button);
	}
}

void UCubelithTitleWidget::RebuildPieceCountOptions()
{
	// 入れ物が無くても選択は正しい値へ寄せる（GeneratePuzzle はプリセット外の M を受け取れない）
	const TArray<int32> Presets = Cubelith::PiecePresets(SpaceSize);
	PieceCount = Cubelith::NearestPreset(Presets, PieceCount);

	PieceCountButtons.Reset();
	if (PieceCountRow == nullptr)
	{
		return;
	}
	PieceCountRow->ClearChildren();

	for (const int32 Value : Presets)
	{
		UButton* const Button = ConstructButton(PieceCountRow, NumberLabel(Value), [this, Value]()
		{
			PieceCount = Value;
			SyncSelection();
		});
		PieceCountButtons.Add(Value, Button);
	}
}

void UCubelithTitleWidget::BuildRotationOptions()
{
	RotationOffButton = nullptr;
	RotationOnButton = nullptr;
	if (RotationRow == nullptr)
	{
		return;
	}
	RotationRow->ClearChildren();

	RotationOffButton = ConstructButton(RotationRow, FText::FromString(TEXT("なし")), [this]()
	{
		bAllowRotation = false;
		SyncSelection();
	});
	RotationOnButton = ConstructButton(RotationRow, FText::FromString(TEXT("あり")), [this]()
	{
		bAllowRotation = true;
		SyncSelection();
	});
}

void UCubelithTitleWidget::SyncSelection()
{
	for (const TPair<int32, TObjectPtr<UButton>>& Pair : SpaceSizeButtons)
	{
		SetButtonSelected(Pair.Value, Pair.Key == SpaceSize);
	}
	for (const TPair<int32, TObjectPtr<UButton>>& Pair : PieceCountButtons)
	{
		SetButtonSelected(Pair.Value, Pair.Key == PieceCount);
	}
	SetButtonSelected(RotationOffButton, !bAllowRotation);
	SetButtonSelected(RotationOnButton, bAllowRotation);

	SetTextSafe(SummaryText,
		FText::FromString(Cubelith::TitleSummaryText(SpaceSize, PieceCount, bAllowRotation)));

	// クリア回数（RULES.md 6 章）。「この難易度」は今選んでいる難易度の回数なので、選択を変えるたびに引き直す
	FCubelithSavedDifficulty Selected;
	Selected.SpaceSize = SpaceSize;
	Selected.PieceCount = PieceCount;
	Selected.bAllowRotation = bAllowRotation;
	SetTextSafe(ClearCountText, FText::FromString(
		Cubelith::TitleClearCountText(Clears.Total, Cubelith::ClearCountOf(Clears, Selected))));

	// シードは出すだけ（この画面では変えない。RULES.md 6 章）
	SetTextSafe(SeedText, FText::FromString(FString::Printf(TEXT("seed %u"), Seed)));
}

void UCubelithTitleWidget::SyncResume()
{
	// 途中の盤面が無ければ「続きから」とその 1 行は出さない（RULES.md 6 章）
	const bool bHasResume = Resume.IsSet();
	SetButtonVisible(ResumeButton, bHasResume);
	SetWidgetVisible(ResumeNoteText, bHasResume);
	if (!bHasResume)
	{
		return;
	}

	// 出すのは**その盤面の**難易度と残りピース数（タイトルで選んでいる難易度ではない。RULES.md 6 章）
	SetTextSafe(ResumeNoteText, FText::FromString(Cubelith::TitleResumeText(Resume.GetValue())));
}

void UCubelithTitleWidget::HandleStartClicked()
{
	FCubelithTitleSelection Selection;
	Selection.SpaceSize = SpaceSize;
	Selection.PieceCount = PieceCount;
	Selection.bAllowRotation = bAllowRotation;
	Selection.Seed = Seed;

	OnStart.ExecuteIfBound(Selection);
}
