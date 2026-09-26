#include "Hint.h"

#include "Grid.h"

namespace Cubelith
{
	namespace
	{
		/** 配置が解答と完全に一致しているか（位置と向きの両方）。TS の matchesSolution */
		bool MatchesSolution(const FPlacement& Placement, const FPlacement& Solution)
		{
			return Placement.Orientation == Solution.Orientation && EqualsVec3(Placement.Position, Solution.Position);
		}
	}

	TOptional<int32> PickHintPiece(
		TArrayView<const FPlacement> Placements, TArrayView<const FPlacement> Solution, TArrayView<const int32> LockedIds)
	{
		// 解答を id 引きの表にする（TS の solutionById）。引くだけで走査しないので、
		// TMap の反復順が結果に効くことはない（Docs/SPEC_UE.md 7.1）
		TMap<int32, const FPlacement*> SolutionById;
		SolutionById.Reserve(Solution.Num());
		for (const FPlacement& Answer : Solution)
		{
			SolutionById.Add(Answer.PieceId, &Answer);
		}

		// 「含むかどうか」だけを見る集合なので TSet でよい（同 7.1）
		TSet<int32> Locked;
		Locked.Reserve(LockedIds.Num());
		for (const int32 LockedId : LockedIds)
		{
			Locked.Add(LockedId);
		}

		TOptional<int32> Fallback;
		TOptional<int32> Target;
		int32 UnlockedCount = 0;

		for (const FPlacement& Placement : Placements)
		{
			const FPlacement* const* Answer = SolutionById.Find(Placement.PieceId);
			// 解答に無い id は呼び出し側のバグ（TS は Error）
			checkf(Answer != nullptr, TEXT("ヒント: 解答に無いピース id %d"), Placement.PieceId);

			if (Locked.Contains(Placement.PieceId))
			{
				continue;
			}

			++UnlockedCount;
			if (!Fallback.IsSet() || Placement.PieceId < Fallback.GetValue())
			{
				Fallback = Placement.PieceId;
			}
			if (MatchesSolution(Placement, **Answer))
			{
				continue;
			}
			if (!Target.IsSet() || Placement.PieceId < Target.GetValue())
			{
				Target = Placement.PieceId;
			}
		}

		// 最後の 1 ピースはヒントを使えない（未固定 0 個 / 1 個は未設定。RULES.md 3.7）
		if (UnlockedCount <= 1)
		{
			return TOptional<int32>();
		}
		return Target.IsSet() ? Target : Fallback;
	}
}
