// CubelithSave.h の実装。検証の条件は移植元 WebMock/src/ui/save.ts の parse* から変えない
// （型そのものの検証だけは、USaveGame のシリアライズが保証するので落としてある）

#include "CubelithSave.h"

#include "CubelithSeed.h"
#include "Generate.h"
#include "Grid.h"

namespace Cubelith
{
	FCubelithSaveData DefaultSaveData()
	{
		// 既定の難易度は FCubelithSavedDifficulty の既定値（RULES.md 3.1 の N=3 / M=4 / 回転なし）
		return FCubelithSaveData();
	}

	FString DifficultyKey(const FCubelithSavedDifficulty& Difficulty)
	{
		return FString::Printf(TEXT("%d-%d-%d"),
			Difficulty.SpaceSize, Difficulty.PieceCount, Difficulty.bAllowRotation ? 1 : 0);
	}

	int32 ClearCountOf(const FCubelithSavedClears& Clears, const FCubelithSavedDifficulty& Difficulty)
	{
		const int32* Found = Clears.ByDifficulty.Find(DifficultyKey(Difficulty));
		return Found != nullptr ? *Found : 0;
	}

	int32 ClearCountOf(const FCubelithSaveData& Data, const FCubelithSavedDifficulty& Difficulty)
	{
		return ClearCountOf(Data.Clears, Difficulty);
	}

	void RecordClear(FCubelithSaveData& Data, const FCubelithSavedDifficulty& Difficulty)
	{
		const int32 Next = ClearCountOf(Data, Difficulty) + 1;
		Data.Difficulty = Difficulty;
		Data.bHasProgress = false;
		Data.Progress = FCubelithSavedProgress();
		Data.Clears.Total += 1;
		Data.Clears.ByDifficulty.Add(DifficultyKey(Difficulty), Next);
	}

	bool IsValidSavedDifficulty(const FCubelithSavedDifficulty& Difficulty)
	{
		if (Difficulty.SpaceSize < MinSpaceSize || Difficulty.SpaceSize > MaxSpaceSize)
		{
			return false;
		}
		// MaxPieces は N が 3..7 の外だと checkf に落ちるので、N を確かめた後に呼ぶ
		return Difficulty.PieceCount >= MinPieceCount && Difficulty.PieceCount <= MaxPieces(Difficulty.SpaceSize);
	}

	bool IsValidSavedProgress(const FCubelithSavedProgress& Progress)
	{
		if (!IsValidSavedDifficulty(Progress.Difficulty))
		{
			return false;
		}
		// 負の値は「保存されていない」（CubelithSave.h の解釈）ので、盤面のシードとしては認めない
		if (Progress.Seed < MinSeedValue || Progress.Seed > MaxSeedValue)
		{
			return false;
		}
		if (Progress.Placements.Num() == 0)
		{
			return false;
		}
		// 生成規則が変わった / 手で書き換えられた場合。ここで弾かないと復元時に FGame が checkf に落ちる
		if (Progress.Placements.Num() != Progress.Difficulty.PieceCount)
		{
			return false;
		}

		TSet<int32> PlacedIds;
		for (const FCubelithSavedPlacement& Placement : Progress.Placements)
		{
			if (Placement.PieceId < 0)
			{
				return false;
			}
			if (Placement.Orientation < 0 || Placement.Orientation >= OrientationCount)
			{
				return false;
			}
			bool bAlready = false;
			PlacedIds.Add(Placement.PieceId, &bAlready);
			if (bAlready)
			{
				return false;
			}
		}

		TSet<int32> LockedIds;
		for (const FCubelithSavedLock& Lock : Progress.Locks)
		{
			if (!PlacedIds.Contains(Lock.PieceId))
			{
				return false;
			}
			bool bAlready = false;
			LockedIds.Add(Lock.PieceId, &bAlready);
			if (bAlready)
			{
				return false;
			}
		}

		return Progress.Remaining >= 0 && Progress.Remaining <= Progress.Placements.Num();
	}

	bool ProgressFitsPieces(const FCubelithSavedProgress& Progress, TArrayView<const int32> PieceIds)
	{
		if (Progress.Placements.Num() != PieceIds.Num())
		{
			return false;
		}
		TSet<int32> Placed;
		Placed.Reserve(Progress.Placements.Num());
		for (const FCubelithSavedPlacement& Placement : Progress.Placements)
		{
			Placed.Add(Placement.PieceId);
		}
		if (Placed.Num() != Progress.Placements.Num())
		{
			return false;
		}
		for (const int32 Id : PieceIds)
		{
			if (!Placed.Contains(Id))
			{
				return false;
			}
		}
		return true;
	}

	FCubelithSaveData SanitizeSaveData(const FCubelithSaveData& Data)
	{
		if (!IsValidSavedDifficulty(Data.Difficulty))
		{
			return DefaultSaveData();
		}

		FCubelithSaveData Result;
		Result.Difficulty = Data.Difficulty;

		if (Data.bHasProgress && IsValidSavedProgress(Data.Progress))
		{
			Result.bHasProgress = true;
			Result.Progress = Data.Progress;
			// 固定は id 昇順で持つ（TS の parseLocks が並べ直すのと同じ。書く側の並びに依存しないため）
			Result.Progress.Locks.Sort([](const FCubelithSavedLock& A, const FCubelithSavedLock& B)
				{
					return A.PieceId < B.PieceId;
				});
		}

		Result.Clears.Total = FMath::Max(0, Data.Clears.Total);
		for (const TPair<FString, int32>& Pair : Data.Clears.ByDifficulty)
		{
			if (Pair.Value >= 0)
			{
				Result.Clears.ByDifficulty.Add(Pair.Key, Pair.Value);
			}
		}

		return Result;
	}

	ELockKind ToCoreLockKind(ECubelithSavedLockKind Kind)
	{
		return Kind == ECubelithSavedLockKind::Hint ? ELockKind::Hint : ELockKind::Manual;
	}

	ECubelithSavedLockKind ToSavedLockKind(ELockKind Kind)
	{
		return Kind == ELockKind::Hint ? ECubelithSavedLockKind::Hint : ECubelithSavedLockKind::Manual;
	}

	FPlacement ToCorePlacement(const FCubelithSavedPlacement& Placement)
	{
		FPlacement Result;
		Result.PieceId = Placement.PieceId;
		Result.Orientation = Placement.Orientation;
		Result.Position = Vec3(Placement.Position.X, Placement.Position.Y, Placement.Position.Z);
		return Result;
	}

	FCubelithSavedPlacement ToSavedPlacement(const FPlacement& Placement)
	{
		FCubelithSavedPlacement Result;
		Result.PieceId = Placement.PieceId;
		Result.Orientation = Placement.Orientation;
		Result.Position = FIntVector(Placement.Position.X, Placement.Position.Y, Placement.Position.Z);
		return Result;
	}

	TArray<FPlacement> ToCorePlacements(TArrayView<const FCubelithSavedPlacement> Placements)
	{
		TArray<FPlacement> Result;
		Result.Reserve(Placements.Num());
		for (const FCubelithSavedPlacement& Placement : Placements)
		{
			Result.Add(ToCorePlacement(Placement));
		}
		return Result;
	}

	TArray<FCubelithSavedLock> CollectSavedLocks(const FGame& Game)
	{
		TArray<FCubelithSavedLock> Result;
		const TArray<int32> LockedIds = Game.LockedIds();
		Result.Reserve(LockedIds.Num());
		for (const int32 PieceId : LockedIds)
		{
			FCubelithSavedLock Lock;
			Lock.PieceId = PieceId;
			// LockedIds が返した id は必ず固定されているが、万一取りこぼしても
			// 手動の固定として書く（TS の `?? 'manual'` と同じ）
			Lock.Kind = ToSavedLockKind(Game.LockKindOf(PieceId).Get(ELockKind::Manual));
			Result.Add(Lock);
		}
		return Result;
	}

	FCubelithSavedProgress MakeSavedProgress(
		const FCubelithSavedDifficulty& Difficulty, uint32 Seed,
		TArrayView<const FPlacement> Placements, TArrayView<const FCubelithSavedLock> Locks, int32 Remaining)
	{
		FCubelithSavedProgress Progress;
		Progress.Difficulty = Difficulty;
		Progress.Seed = static_cast<int64>(Seed);
		Progress.Placements.Reserve(Placements.Num());
		for (const FPlacement& Placement : Placements)
		{
			Progress.Placements.Add(ToSavedPlacement(Placement));
		}
		Progress.Locks.Append(Locks.GetData(), Locks.Num());
		Progress.Remaining = Remaining;
		return Progress;
	}

	void SetProgress(FCubelithSaveData& Data, const FCubelithSavedProgress& Progress)
	{
		// 難易度が決まるのは盤面を始めたとき（RULES.md 3.8）。TS の withProgress と同じく、
		// 盤面を書くときに「最後に選んだ難易度」もその盤面の難易度へ揃える
		Data.Difficulty = Progress.Difficulty;
		Data.bHasProgress = true;
		Data.Progress = Progress;
	}
}
