// プレイ中 HUD（RULES.md 6 章）の「どのボタンを出すか・押せるか・ラベルは何か」を決める純粋な判断。
// 移植元は WebMock/src/ui/hud.ts の render() と setRemaining で、UMG とも UObject とも切り離して
// テストできるようにこのファイルへ切り出してある（CubelithLockOps.h / CubelithProgress.h と同じ立ち位置）。
//
// 表示の条件は RULES.md 6 章のとおり:
//   - 回転モードのトグルと「固定 / 固定解除」は、ピースを選択しているときだけ出す
//   - 回転モードのトグルはパズルの回転「あり」のときだけ出す
//   - 固定中のピースを選んでいる間は回転モードのトグルを押せない
//   - ヒントで固定したピースを選んでいる間は「固定解除」を押せない
//   - 「ヒント」は使えないとき（未固定のピースが 1 個以下。RULES.md 3.7）は押せない

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

#include "Game.h"

namespace Cubelith
{
	/**
	 * HUD の表示を決める材料（hud.ts が render() で見ている状態をそのまま 1 つにまとめたもの）。
	 * 残りピース数だけは数え上げが別（CubelithProgress.h）なので HudRemainingText で扱う。
	 */
	struct FHudState
	{
		/** 選択中のピース id（未選択は INDEX_NONE） */
		int32 SelectedPieceId = INDEX_NONE;

		/** 選択中のピースの固定（未固定 / 未選択なら未設定）。Cubelith::FGame::LockKindOf の戻り値そのまま */
		TOptional<ELockKind> LockKind;

		/** この盤面がパズルの回転「あり」か（RULES.md 3.1。ACubelithGameMode::IsRotationAllowed） */
		bool bAllowRotation = false;

		/** 回転モードが入っているか（入れたかどうかを決めるのは入力側。ACubelithPlayerController::IsRotateModeOn） */
		bool bRotateModeOn = false;

		/** ヒントが使えるか（RULES.md 3.7。ACubelithGameMode::IsHintAvailable） */
		bool bHintAvailable = false;
	};

	/**
	 * 上の状態から決まる HUD の見せ方（hud.ts の render() が DOM へ書き込んでいる内容）。
	 * ラベルは FString で返す（FText の作り分けは呼び出し側の UMG に任せる）。
	 */
	struct FHudDisplay
	{
		/** ピースに紐づく操作の行（回転モードのトグルと「固定 / 固定解除」）を出すか */
		bool bShowPieceControls = false;

		/** 回転モードのトグルを出すか（パズルの回転「あり」のときだけ。RULES.md 6 章） */
		bool bShowRotateMode = false;

		/** 回転モードのトグルを押せるか（固定中のピースを選んでいる間は押せない） */
		bool bRotateModeEnabled = false;

		/** 回転モードが効いているか（ラベルが「回転解除」になり、選ばれている見せ方になる） */
		bool bRotateModeActive = false;

		/** 「固定 / 固定解除」を押せるか（ヒントで固定したピースは解除できないので押せない） */
		bool bLockEnabled = false;

		/** 手動の固定が付いているか（選ばれている見せ方にする） */
		bool bLockActive = false;

		/** 「ヒント」を押せるか */
		bool bHintEnabled = false;

		/** 回転モードのトグルのラベル（RULES.md 6 章の「回転 / 回転解除」） */
		FString RotateModeLabel;

		/** 「固定 / 固定解除」のラベル */
		FString LockLabel;

		/** 今できる操作の案内（hud.ts の #hud-hint） */
		FString StatusText;
	};

	/** 回転モードのトグルのラベル（オフ） */
	CUBELITH_API extern const TCHAR* const HudRotateModeLabelOff;

	/** 回転モードのトグルのラベル（オン） */
	CUBELITH_API extern const TCHAR* const HudRotateModeLabelOn;

	/** 「固定」のラベル（未固定） */
	CUBELITH_API extern const TCHAR* const HudLockLabelLock;

	/** 「固定解除」のラベル（固定中） */
	CUBELITH_API extern const TCHAR* const HudLockLabelUnlock;

	/** HUD の見せ方を決める（hud.ts の render）。状態を渡すだけで、UMG には触らない */
	CUBELITH_API FHudDisplay ResolveHudDisplay(const FHudState& State);

	/**
	 * 残りピース数の表示（RULES.md 6 章の「残りピース数（未確定のピース数）」。hud.ts の setRemaining）。
	 * 数え方は CubelithProgress.h の「解釈:」（Cubelith::UnsettledPieceCount）。
	 */
	CUBELITH_API FString HudRemainingText(int32 Remaining, int32 Total);
}
