#include "Game.h"

#include "Algo/StableSort.h"
#include "Solve.h"

namespace Cubelith
{
	FPlacement MovePlacement(const FPlacement& Placement, const FVec3& Delta)
	{
		FPlacement Result;
		Result.PieceId = Placement.PieceId;
		Result.Orientation = Placement.Orientation;
		Result.Position = AddVec3(Placement.Position, Delta);
		return Result;
	}

	FPlacement RotatePlacement(const FPlacement& Placement, EAxis Axis, int32 Dir)
	{
		FPlacement Result;
		Result.PieceId = Placement.PieceId;
		Result.Orientation = RotateOrientation(Placement.Orientation, Axis, Dir);
		Result.Position = Placement.Position;
		return Result;
	}

	TArray<FPlacement> ReplacePlacement(TArrayView<const FPlacement> Placements, const FPlacement& Next)
	{
		bool bFound = false;

		TArray<FPlacement> Result;
		Result.Reserve(Placements.Num());
		for (const FPlacement& Placement : Placements)
		{
			if (Placement.PieceId != Next.PieceId)
			{
				Result.Add(Placement);
				continue;
			}
			bFound = true;
			Result.Add(Next);
		}

		checkf(bFound, TEXT("配置の差し替え: 未知のピース id %d"), Next.PieceId);
		return Result;
	}

	FGame::FGame(TArrayView<const FPiece> Pieces, int32 InN, TArrayView<const FPlacement> Initial, FGameChangeListener OnChange)
		: PieceList(Pieces.GetData(), Pieces.Num())
		, SpaceSize(InN)
		, OnChangeListener(MoveTemp(OnChange))
	{
		KnownIds.Reserve(PieceList.Num());
		for (const FPiece& Piece : PieceList)
		{
			KnownIds.Add(Piece.Id);
		}

		Current = Validate(Initial);
		bSolvedFlag = IsSolved(PieceList, Current, SpaceSize);
		// 構築時には OnChange を呼ばない（初回の描画は呼び出し側が Placements() から行う）
	}

	TArray<FPlacement> FGame::Validate(TArrayView<const FPlacement> Placements) const
	{
		// 「含むかどうか」だけを見る集合なので TSet でよい（Docs/SPEC_UE.md 7.1）
		TSet<int32> Seen;
		Seen.Reserve(Placements.Num());
		for (const FPlacement& Placement : Placements)
		{
			checkf(KnownIds.Contains(Placement.PieceId), TEXT("配置: 未知のピース id %d"), Placement.PieceId);
			checkf(!Seen.Contains(Placement.PieceId), TEXT("配置: ピース id %d が重複している"), Placement.PieceId);
			Seen.Add(Placement.PieceId);
		}
		checkf(Seen.Num() == KnownIds.Num(), TEXT("配置の数が足りない (ピース %d / 配置 %d)"), KnownIds.Num(), Seen.Num());

		return TArray<FPlacement>(Placements.GetData(), Placements.Num());
	}

	void FGame::AssertKnown(int32 PieceId) const
	{
		checkf(KnownIds.Contains(PieceId), TEXT("未知のピース id %d"), PieceId);
	}

	void FGame::Apply(TArray<FPlacement>&& Next)
	{
		Current = MoveTemp(Next);
		bSolvedFlag = IsSolved(PieceList, Current, SpaceSize);
		if (OnChangeListener)
		{
			OnChangeListener(Current, bSolvedFlag);
		}
	}

	const FPlacement& FGame::PlacementFor(int32 PieceId) const
	{
		const FPlacement* Placement = PlacementOf(PieceId);
		checkf(Placement != nullptr, TEXT("未知のピース id %d"), PieceId);
		return *Placement;
	}

	const FPlacement* FGame::PlacementOf(int32 PieceId) const
	{
		return Current.FindByPredicate([PieceId](const FPlacement& Placement) -> bool { return Placement.PieceId == PieceId; });
	}

	void FGame::Move(int32 PieceId, const FVec3& Delta)
	{
		const FPlacement& Placement = PlacementFor(PieceId);
		// 固定中のピースは動かせない。何も変わらないので OnChange も呼ばない（RULES.md 3.3）
		if (Locks.Contains(PieceId))
		{
			return;
		}
		Apply(ReplacePlacement(Current, MovePlacement(Placement, Delta)));
	}

	void FGame::Rotate(int32 PieceId, EAxis Axis, int32 Dir)
	{
		const FPlacement& Placement = PlacementFor(PieceId);
		if (Locks.Contains(PieceId))
		{
			return;
		}
		Apply(ReplacePlacement(Current, RotatePlacement(Placement, Axis, Dir)));
	}

	void FGame::Place(int32 PieceId, int32 Orientation, const FVec3& Position)
	{
		AssertKnown(PieceId);
		OrientationMatrix(Orientation); // 向き id が 0..23 かをここで確かめる
		if (Locks.Contains(PieceId))
		{
			return;
		}

		FPlacement Next;
		Next.PieceId = PieceId;
		Next.Orientation = Orientation;
		Next.Position = Position;
		Apply(ReplacePlacement(Current, Next));
	}

	void FGame::Lock(int32 PieceId, ELockKind Kind)
	{
		AssertKnown(PieceId);
		Locks.Add(PieceId, Kind);
		// 固定しても配置は変わらないので OnChange は呼ばない（見た目の更新は呼び出し側の責務）
	}

	void FGame::Unlock(int32 PieceId)
	{
		AssertKnown(PieceId);
		// Hint の固定は解除できない（ヒントで置いたピースは動かせないまま。RULES.md 3.3）
		const ELockKind* Kind = Locks.Find(PieceId);
		if (Kind != nullptr && *Kind == ELockKind::Manual)
		{
			Locks.Remove(PieceId);
		}
		// 解除も配置を変えないので OnChange は呼ばない
	}

	TOptional<ELockKind> FGame::LockKindOf(int32 PieceId) const
	{
		// TS の lockKindOf は未知の id でも例外にせず null を返すので、ここでも AssertKnown しない
		const ELockKind* Kind = Locks.Find(PieceId);
		return Kind != nullptr ? TOptional<ELockKind>(*Kind) : TOptional<ELockKind>();
	}

	TArray<int32> FGame::LockedIds() const
	{
		TArray<int32> Ids;
		Locks.GetKeys(Ids);
		// TMap の反復順は要素の出入りで変わりうるので、昇順に直してから返す（TS の sort と同じ結果）
		Algo::StableSort(Ids, [](int32 A, int32 B) -> bool { return A < B; });
		return Ids;
	}

	void FGame::Reset(TArrayView<const FPlacement> Placements)
	{
		TArray<FPlacement> Next = Validate(Placements);

		// 散らし直しでは人の固定は解け、ヒントの固定は残る（残す配置は呼び出し側が Keep で作る。RULES.md 3.3）
		for (auto It = Locks.CreateIterator(); It; ++It)
		{
			if (It.Value() == ELockKind::Manual)
			{
				It.RemoveCurrent();
			}
		}

		Apply(MoveTemp(Next));
	}

	FGame CreateGame(TArrayView<const FPiece> Pieces, int32 N, TArrayView<const FPlacement> Initial, FGameChangeListener OnChange)
	{
		return FGame(Pieces, N, Initial, MoveTemp(OnChange));
	}
}
