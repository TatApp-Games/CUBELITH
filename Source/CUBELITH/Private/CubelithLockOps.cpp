#include "CubelithLockOps.h"

namespace Cubelith
{
	ELockToggleAction DecideLockToggle(const TOptional<ELockKind>& Current)
	{
		if (!Current.IsSet())
		{
			// 未固定 → 手動の固定を付ける
			return ELockToggleAction::Lock;
		}

		// ヒントで置いたピースの固定は解除できない（RULES.md 3.3）。FGame::Unlock も Hint には効かないが、
		// ボタンの押下を弾いたことを呼び出し側に見せられるよう、ここで別の値として返す
		return (Current.GetValue() == ELockKind::Hint) ? ELockToggleAction::Blocked : ELockToggleAction::Unlock;
	}

	bool IsHintAvailable(int32 PieceCount, int32 LockedCount)
	{
		// 未固定が 2 個以上（TS の total - lockedIds().length >= 2）。負の数を渡されても真にならないよう素直に引く
		return (PieceCount - LockedCount) >= 2;
	}

	TArray<FPlacement> CollectHintKeptPlacements(
		TArrayView<const FPlacement> Placements, TFunctionRef<TOptional<ELockKind>(int32)> LockKindOf)
	{
		TArray<FPlacement> Kept;

		for (const FPlacement& Placement : Placements)
		{
			const TOptional<ELockKind> Kind = LockKindOf(Placement.PieceId);
			if (Kind.IsSet() && Kind.GetValue() == ELockKind::Hint)
			{
				Kept.Add(Placement);
			}
		}

		return Kept;
	}

	const FLinearColor& LockIconColor(
		ELockKind Kind, const FLinearColor& ManualColor, const FLinearColor& HintColor)
	{
		return (Kind == ELockKind::Hint) ? HintColor : ManualColor;
	}
}
