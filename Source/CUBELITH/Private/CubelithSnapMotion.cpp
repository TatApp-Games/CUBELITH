#include "CubelithSnapMotion.h"

namespace Cubelith
{
	double EaseOutCubic(double T)
	{
		// TS の 1 - (1 - t) ** 3 と同じ
		const double Inverted = 1.0 - T;
		return 1.0 - Inverted * Inverted * Inverted;
	}

	FSnapMotion::FSnapMotion(double InDurationSeconds)
		: Duration(InDurationSeconds)
	{
	}

	void FSnapMotion::Start(int32 PieceId, const FVec3& Offset, double NowSeconds, TArray<FSnapOffsetUpdate>& OutUpdates)
	{
		OutUpdates.Reset();

		if (Offset.X == 0 && Offset.Y == 0 && Offset.Z == 0)
		{
			// ずれが無い（吸着で位置が変わらなかった）なら補間する物が無い。走っていた分は畳む
			Clear(PieceId, OutUpdates);
			return;
		}

		// 同じピースに 2 本走らせない（前の補間はここで捨てられる。TS の tweens.set と同じ）
		FTween Tween;
		Tween.Offset = Offset;
		Tween.StartSeconds = NowSeconds;
		Tweens.Add(PieceId, Tween);

		// 開始直後はずれが満額（見た目が移動前の位置から始まる）
		FSnapOffsetUpdate Update;
		Update.PieceId = PieceId;
		Update.Offset = FVector(Offset.X, Offset.Y, Offset.Z);
		OutUpdates.Add(Update);
	}

	void FSnapMotion::Update(double NowSeconds, TArray<FSnapOffsetUpdate>& OutUpdates)
	{
		OutUpdates.Reset();

		if (Tweens.Num() == 0)
		{
			return;
		}

		// Clear が Tweens から消すので、走っている id を先に写してから回す（TS の [...tweens]）
		TArray<int32> PieceIds;
		Tweens.GetKeys(PieceIds);

		for (const int32 PieceId : PieceIds)
		{
			const FTween* Tween = Tweens.Find(PieceId);
			if (Tween == nullptr)
			{
				continue;
			}

			const double T = (Duration > 0.0) ? (NowSeconds - Tween->StartSeconds) / Duration : 1.0;
			if (T >= 1.0)
			{
				// 所要時間を過ぎたらずれ無しに戻す（論理位置そのまま）
				Clear(PieceId, OutUpdates);
				continue;
			}

			// 時刻が戻された（開始より前）ときは満額のまま。TS の Math.max(t, 0) と同じ
			const double Remaining = 1.0 - EaseOutCubic(FMath::Max(T, 0.0));

			FSnapOffsetUpdate Result;
			Result.PieceId = PieceId;
			Result.Offset = FVector(
				static_cast<double>(Tween->Offset.X) * Remaining,
				static_cast<double>(Tween->Offset.Y) * Remaining,
				static_cast<double>(Tween->Offset.Z) * Remaining);
			OutUpdates.Add(Result);
		}
	}

	void FSnapMotion::Cancel(int32 PieceId, TArray<FSnapOffsetUpdate>& OutUpdates)
	{
		OutUpdates.Reset();
		Clear(PieceId, OutUpdates);
	}

	void FSnapMotion::CancelAll(TArray<FSnapOffsetUpdate>& OutUpdates)
	{
		OutUpdates.Reset();

		TArray<int32> PieceIds;
		Tweens.GetKeys(PieceIds);
		// 打ち切りの順序は表示に影響しないが、ログや確かめが安定するよう id 昇順にする
		PieceIds.Sort();
		for (const int32 PieceId : PieceIds)
		{
			Clear(PieceId, OutUpdates);
		}
	}

	void FSnapMotion::Clear(int32 PieceId, TArray<FSnapOffsetUpdate>& OutUpdates)
	{
		// 走っていなければ書き戻す必要も無い（通常時に無駄な描き直しを出さない。TS の clear と同じ）
		if (Tweens.Remove(PieceId) == 0)
		{
			return;
		}

		FSnapOffsetUpdate Update;
		Update.PieceId = PieceId;
		Update.bCleared = true;
		OutUpdates.Add(Update);
	}
}
