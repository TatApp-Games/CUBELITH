// ドラッグ方向 → グリッド軸の写像（移植元 WebMock/src/input/axisMapping.ts）

#include "CubelithAxisMapping.h"

namespace Cubelith
{
	namespace
	{
		/** 候補にする 3 軸。同点のときの決定的な順序（x → y → z）もこの並びで決まる */
		constexpr EAxis AllAxes[3] = { EAxis::X, EAxis::Y, EAxis::Z };

		/** ベクトルの軸成分（TS の component） */
		double Component(const FVector& V, EAxis Axis)
		{
			if (Axis == EAxis::X)
			{
				return V.X;
			}
			if (Axis == EAxis::Y)
			{
				return V.Y;
			}
			return V.Z;
		}

		/**
		 * |成分| が最大の軸を選ぶ（TS の dominantAxis）。
		 * ExcludeCount 個までの軸を候補から外す。同点は x → y → z の順で決定的に選ぶ
		 * （更新を厳密な > で行うので、先に見た軸が残る）。
		 */
		FAxisStep DominantAxis(const FVector& V, const EAxis* Exclude, int32 ExcludeCount)
		{
			EAxis Best = EAxis::X;
			bool bFound = false;
			double BestMagnitude = -1.0;

			for (const EAxis Axis : AllAxes)
			{
				bool bExcluded = false;
				for (int32 Index = 0; Index < ExcludeCount; ++Index)
				{
					if (Exclude[Index] == Axis)
					{
						bExcluded = true;
						break;
					}
				}
				if (bExcluded)
				{
					continue;
				}

				const double Magnitude = FMath::Abs(Component(V, Axis));
				if (Magnitude > BestMagnitude)
				{
					Best = Axis;
					bFound = true;
					BestMagnitude = Magnitude;
				}
			}

			// 除外が 2 軸までなら必ず 1 つ残る（TS が throw しているのと同じ不変条件）
			checkf(bFound, TEXT("軸の候補が残っていない"));

			FAxisStep Step;
			Step.Axis = Best;
			// 成分が 0 のときは向きを決められないので + を既定にする
			Step.Sign = Component(V, Best) < 0.0 ? -1 : 1;
			return Step;
		}
	}

	FDragAxes DragAxes(const FVector& Right, const FVector& Up, const FVector& Forward)
	{
		FDragAxes Axes;
		Axes.Right = DominantAxis(Right, nullptr, 0);

		const EAxis ExcludeForUp[1] = { Axes.Right.Axis };
		Axes.Up = DominantAxis(Up, ExcludeForUp, 1);

		const EAxis ExcludeForDepth[2] = { Axes.Right.Axis, Axes.Up.Axis };
		Axes.Depth = DominantAxis(Forward, ExcludeForDepth, 2);

		return Axes;
	}

	FVec3 AxisStepVector(const FAxisStep& Step, int32 Count)
	{
		const int32 Value = Step.Sign * Count;
		if (Step.Axis == EAxis::X)
		{
			return Vec3(Value, 0, 0);
		}
		if (Step.Axis == EAxis::Y)
		{
			return Vec3(0, Value, 0);
		}
		return Vec3(0, 0, Value);
	}
}
