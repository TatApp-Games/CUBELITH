// タイトル / 難易度選択画面（RULES.md 6 章）に出す文言の材料。
// 移植元は WebMock/src/ui/titleScreen.ts の rotationLabel / resumeNote / clearLine で、UMG とも
// UObject とも切り離してテストできるようにこのファイルへ切り出してある
// （CubelithHudState.h / CubelithLockOps.h / CubelithProgress.h と同じ立ち位置）。
//
// RULES.md 6 章のうち、このファイルが受け持つのは次の 2 行:
//   - 途中の盤面（3.8）があれば「開始」の上に「続きから」を出し、その盤面の難易度と残りピース数を添える
//   - クリア回数（3.8）を「合計」と「選んでいる難易度の回数」で 1 行出す

#pragma once

#include "CoreMinimal.h"

#include "CubelithSave.h"

namespace Cubelith
{
	/**
	 * タイトルの「続きから」に添える 1 行の材料（titleScreen.ts の TitleResume）。
	 * 盤面そのもの（配置と固定）は持たない ＝ タイトルでは盤面を再生成しない
	 */
	struct FTitleResume
	{
		/** 途中の盤面の空間サイズ N */
		int32 SpaceSize = 3;

		/** 途中の盤面のピース分割数 M */
		int32 PieceCount = 4;

		/** 途中の盤面のパズルの回転 */
		bool bAllowRotation = false;

		/** 残りピース数（未確定のピース数。RULES.md 6 章。保存された値をそのまま出す） */
		int32 Remaining = 0;
	};

	/**
	 * パズルの回転の表示（titleScreen.ts の rotationLabel）。
	 * まとめの行と「続きから」の 1 行で同じ文言を使う
	 */
	CUBELITH_API const TCHAR* RotationLabel(bool bAllowRotation);

	/** 保存された盤面から「続きから」の材料を作る（盤面の中身は見ない） */
	CUBELITH_API FTitleResume MakeTitleResume(const FCubelithSavedProgress& Progress);

	/**
	 * 「続きから」に添える 1 行（`N = 3 / M = 4 / 回転なし・残り 2 ピース`。titleScreen.ts の resumeNote）。
	 * 出すのは**その盤面の**難易度で、タイトルで選んでいる難易度ではない（RULES.md 6 章）
	 */
	CUBELITH_API FString TitleResumeText(const FTitleResume& Resume);

	/**
	 * クリア回数の 1 行（`クリア 合計 3 回 / この難易度 1 回`。titleScreen.ts の clearLine）。
	 * 「この難易度」は**タイトルで選んでいる**難易度の回数なので、選択を変えるたびに作り直す
	 */
	CUBELITH_API FString TitleClearCountText(int32 Total, int32 CountOfSelected);

	/**
	 * 選んでいる条件のまとめの 1 行（`N = 3 / M = 4 / 回転なし（27 ボクセルを 4 個に分割）`。
	 * titleScreen.ts の summary）
	 */
	CUBELITH_API FString TitleSummaryText(int32 SpaceSize, int32 PieceCount, bool bAllowRotation);
}
