#include "CubelithSnapControl.h"

#include "Grid.h"
#include "Solve.h"

namespace Cubelith
{
	FSnapControl::FSnapControl(TArrayView<const FPiece> InPieces, int32 InN)
		: PieceList(InPieces)
		, SpaceSize(InN)
	{
	}

	bool FSnapControl::Refresh(TArrayView<const FPlacement> Placements, int32 PieceId)
	{
		// 候補があるピースだけを光らせる（RULES.md 5.1）。TS の refresh と同じく、
		// 候補が無ければ「対象なし」へ戻す
		return SetHint(Compute(Placements, PieceId).IsSet() ? PieceId : INDEX_NONE);
	}

	TOptional<FSnapTarget> FSnapControl::Release(TArrayView<const FPlacement> Placements, int32 PieceId)
	{
		// 離した時点の配置で計算し直す（Refresh 後にドラッグが進んでいることがある。TS の release）
		const TOptional<FPlacement> Candidate = Compute(Placements, PieceId);

		const FPlacement* From = nullptr;
		for (const FPlacement& Placement : Placements)
		{
			if (Placement.PieceId == PieceId)
			{
				From = &Placement;
				break;
			}
		}

		// 吸着したかどうかによらず発光は消す（手を離したらもう候補を見せる意味が無い）
		SetHint(INDEX_NONE);

		if (!Candidate.IsSet() || From == nullptr)
		{
			return TOptional<FSnapTarget>();
		}

		FSnapTarget Target;
		Target.From = *From;
		Target.To = *Candidate;
		return Target;
	}

	TOptional<FPlacement> FSnapControl::Compute(TArrayView<const FPlacement> Placements, int32 PieceId) const
	{
		if (PieceId == INDEX_NONE || PieceList.Num() == 0)
		{
			return TOptional<FPlacement>();
		}

		const FPlacement* Active = nullptr;
		for (const FPlacement& Placement : Placements)
		{
			if (Placement.PieceId == PieceId)
			{
				Active = &Placement;
				break;
			}
		}
		if (Active == nullptr)
		{
			// SnapCandidate はアクティブなピースの配置漏れを checkf で弾く（Solve.h）。
			// ここへ来るのは「まだ配置が来ていない」場合もあるので、停止せず「候補なし」として扱う
			return TOptional<FPlacement>();
		}

		const TOptional<FPlacement> Candidate = SnapCandidate(PieceList, Placements, PieceId, SpaceSize);
		if (!Candidate.IsSet() || EqualsVec3(Candidate->Position, Active->Position))
		{
			// 現在位置がそのまま候補（＝すでに収まっている）なら吸着先ではない。
			// SnapCandidate は移動量 0 の候補も返す仕様（Solve.h）なので、ここで捨てる（TS と同じ）
			return TOptional<FPlacement>();
		}

		return Candidate;
	}

	bool FSnapControl::SetHint(int32 PieceId)
	{
		if (HintedPiece == PieceId)
		{
			return false;
		}

		HintedPiece = PieceId;
		return true;
	}
}
