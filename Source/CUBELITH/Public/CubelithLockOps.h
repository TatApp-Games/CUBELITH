// ピースの固定 / 固定解除・ヒント・散らし直し（RULES.md 3.3 / 3.7）を組み立てるときの純粋な判断。
// 本体（固定の保持・ヒント対象の選定・散らし直しの配置作り）は CUBELITHCore に移植済みなので
// （Cubelith::FGame::Lock / Unlock、Cubelith::PickHintPiece、Cubelith::ScatterPlacements）、
// ここに置くのは「操作として繋ぐときに要る小さな判断」だけ。移植元は WebMock/src/main.ts の
// onToggleLock / onHint / onReset と refreshHintEnabled で、UI とも UObject とも切り離して
// テストできるようにこのファイルへ切り出してある（CubelithProgress.h / CubelithDifficulty.h と同じ立ち位置）。

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"

#include "Game.h"
#include "Piece.h"

namespace Cubelith
{
	/**
	 * 「固定 / 固定解除」を押したときに実際に行うこと（TS の onToggleLock の分岐）。
	 * UENUM にはしない（Blueprint へ出す必要が無く、`.uasset` も作らない。ECubelithScreen と同じ扱い）。
	 */
	enum class ELockToggleAction : uint8
	{
		/** 未固定だったので手動の固定（ELockKind::Manual）を付ける */
		Lock,
		/** 手動の固定が付いていたので外す */
		Unlock,
		/** ヒントの固定が付いているので何もしない（**解除できない**。RULES.md 3.3） */
		Blocked,
	};

	/**
	 * 今の固定の種類から、「固定 / 固定解除」で何をするかを決める（RULES.md 3.3）。
	 * Current は Cubelith::FGame::LockKindOf の戻り値そのまま（未設定 = 未固定）。
	 */
	CUBELITH_API ELockToggleAction DecideLockToggle(const TOptional<ELockKind>& Current);

	/**
	 * ヒントが使えるか（RULES.md 3.7。TS の refreshHintEnabled）。
	 *
	 * 未固定のピースが 2 個以上あるときだけ使える（最後の 1 ピースをヒントで埋めると操作せず
	 * クリアできてしまうため）。HUD がボタンの有効 / 無効に使う。
	 * Cubelith::PickHintPiece が未設定を返す条件と同じことを、盤面を渡さずに数だけで判断する。
	 */
	CUBELITH_API bool IsHintAvailable(int32 PieceCount, int32 LockedCount);

	/**
	 * 散らし直し（RULES.md 3.3「やり直し」）で散らさずに残す配置を集める（TS の onReset の keep）。
	 * そのまま Cubelith::FScatterOptions::Keep へ入れる。
	 *
	 * 解釈: 残すのは「ヒントの固定が付いているピースの**現在の**配置」。ヒントは解答位置へ置いてから
	 * 固定するので解答配置と同じ値になるが、Web 版の main.ts と同じく現在の配置から作る
	 * （どちらを正にするかで迷わないよう、盤面 1 つだけを見る形に揃えてある）。
	 * 手動の固定は残さない（Cubelith::FGame::Reset が手動の固定を解くのと揃う）。
	 *
	 * LockKindOf は「ピース id → 固定の種類」を返すもの（Cubelith::FGame::LockKindOf をそのまま包める）。
	 * 並びは Placements の並びを保つ（ScatterPlacements は Keep の並びに依らないが、決めておく）。
	 */
	CUBELITH_API TArray<FPlacement> CollectHintKeptPlacements(
		TArrayView<const FPlacement> Placements, TFunctionRef<TOptional<ELockKind>(int32)> LockKindOf);

	/**
	 * 固定の種類から鍵アイコンの色を選ぶ（RULES.md 6 章。銀 = 手動の固定、金 = ヒントで置いたもの）。
	 * 色そのものは人がエディタで調整する値（ACubelithPuzzleActor の
	 * ManualLockIconColor / HintLockIconColor）なので、ここは「どちらを使うか」だけを決める。
	 */
	CUBELITH_API const FLinearColor& LockIconColor(
		ELockKind Kind, const FLinearColor& ManualColor, const FLinearColor& HintColor);
}
