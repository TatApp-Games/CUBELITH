// 近接ピックのサンプル点（移植元 WebMock/src/input/pickSamples.ts）

#include "CubelithPickSamples.h"

namespace Cubelith
{
	namespace
	{
		/** ずらし無し（中心）。TS の CENTER */
		FPickOffset Center()
		{
			return FPickOffset{ 0.0, 0.0 };
		}

		/** 中心 1 点だけの配列。不正な引数のときのフォールバック */
		TArray<FPickOffset> CenterOnly()
		{
			return TArray<FPickOffset>{ Center() };
		}
	}

	TArray<FPickOffset> PickSampleOffsets(double Radius, double Points, double Rings)
	{
		// Number.isFinite と同じ判定（NaN も ±Inf も弾く）
		if (!FMath::IsFinite(Radius) || Radius <= 0.0)
		{
			return CenterOnly();
		}
		if (!FMath::IsFinite(Points) || !FMath::IsFinite(Rings))
		{
			return CenterOnly();
		}

		const double FlooredPoints = FMath::FloorToDouble(Points);
		const double FlooredRings = FMath::FloorToDouble(Rings);
		if (FlooredPoints < 1.0 || FlooredRings < 1.0)
		{
			return CenterOnly();
		}
		// 解釈: TS には無い上限。TS はこの値の分だけ素直に回すので巨大な値では止まらなくなるが、
		// C++ では int32 への変換が未定義動作になるため、現実に来ない大きさは中心 1 点へ落とす
		// （呼び出し側が渡すのは既定値か設定値で、数十点を超えることはない）
		constexpr double CountLimit = 4096.0;
		if (FlooredPoints > CountLimit || FlooredRings > CountLimit)
		{
			return CenterOnly();
		}

		const int32 PointCount = static_cast<int32>(FlooredPoints);
		const int32 RingCount = static_cast<int32>(FlooredRings);

		TArray<FPickOffset> Offsets;
		Offsets.Reserve(1 + PointCount * RingCount);
		Offsets.Add(Center());

		const double Step = (UE_DOUBLE_PI * 2.0) / static_cast<double>(PointCount);
		for (int32 Ring = 1; Ring <= RingCount; ++Ring)
		{
			const double RingRadius = (Radius * static_cast<double>(Ring)) / static_cast<double>(RingCount);
			// 内側のリングとの位相差。2 本目以降が 1 本目の点の「間」を埋めるようにする
			const double Phase = (Step * static_cast<double>(Ring - 1)) / static_cast<double>(RingCount);
			for (int32 K = 0; K < PointCount; ++K)
			{
				const double Angle = Phase + Step * static_cast<double>(K);
				Offsets.Add(FPickOffset{ RingRadius * FMath::Cos(Angle), RingRadius * FMath::Sin(Angle) });
			}
		}

		return Offsets;
	}
}
