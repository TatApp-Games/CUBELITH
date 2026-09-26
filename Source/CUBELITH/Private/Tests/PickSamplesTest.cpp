// CubelithPickSamples（近接ピックのサンプル点）のテスト。
// 移植元は WebMock/tests/pickSamples.test.ts。並び（先頭が中心・内側のリングから）・半径・
// リングごとの位相ずらし・不正な引数のフォールバックを確かめる。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithPickSamples.h"

#include <limits>

namespace CubelithRenderTests
{
	namespace
	{
		/** 浮動小数の比較に使う許容誤差（TS の toBeCloseTo は小数 2 桁なのでそれより厳しくしてある） */
		constexpr double PickTolerance = 1.0e-9;

		/** 原点からの距離。リングの半径どおりかを見るのに使う */
		double OffsetLength(const Cubelith::FPickOffset& Offset)
		{
			return FMath::Sqrt(Offset.Dx * Offset.Dx + Offset.Dy * Offset.Dy);
		}

		double OffsetAngle(const Cubelith::FPickOffset& Offset)
		{
			return FMath::Atan2(Offset.Dy, Offset.Dx);
		}

		FString Describe(const Cubelith::FPickOffset& Offset)
		{
			return FString::Printf(TEXT("(%f, %f)"), Offset.Dx, Offset.Dy);
		}

		bool IsCenter(const Cubelith::FPickOffset& Offset)
		{
			return FMath::IsNearlyZero(Offset.Dx, PickTolerance) && FMath::IsNearlyZero(Offset.Dy, PickTolerance);
		}

		/** 名前は Unity ビルドで他の翻訳単位と混ざらないよう Pick 接頭辞を付けてある */
		double PickNaN()
		{
			return std::numeric_limits<double>::quiet_NaN();
		}

		double PickInfinity()
		{
			return std::numeric_limits<double>::infinity();
		}
	}
}

// 1. 先頭は必ず中心 (0, 0)、個数は点数 × リング本数 + 1
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPickSamplesLayoutTest, "CUBELITH.Render.PickSamples.Layout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPickSamplesLayoutTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	// 先頭は必ず中心
	if (!IsCenter(Cubelith::PickSampleOffsets(16.0)[0]))
	{
		AddError(TEXT("PickSampleOffsets(16) の先頭が中心でない"));
	}
	if (!IsCenter(Cubelith::PickSampleOffsets(8.0, 4.0, 1.0)[0]))
	{
		AddError(TEXT("PickSampleOffsets(8, 4, 1) の先頭が中心でない"));
	}
	if (!IsCenter(Cubelith::PickSampleOffsets(0.0)[0]))
	{
		AddError(TEXT("PickSampleOffsets(0) の先頭が中心でない"));
	}

	// 点数とリング本数どおりの個数
	TestEqual(TEXT("PickSampleOffsets(16, 6, 2) の個数"), Cubelith::PickSampleOffsets(16.0, 6.0, 2.0).Num(), 1 + 6 * 2);
	TestEqual(TEXT("PickSampleOffsets(16, 8, 1) の個数"), Cubelith::PickSampleOffsets(16.0, 8.0, 1.0).Num(), 1 + 8);
	TestEqual(TEXT("PickSampleOffsets(16, 1, 3) の個数"), Cubelith::PickSampleOffsets(16.0, 1.0, 3.0).Num(), 1 + 3);

	// 既定のサンプル数は十数本に収まる
	const int32 DefaultCount = Cubelith::PickSampleOffsets(16.0).Num();
	if (DefaultCount > 16)
	{
		AddError(FString::Printf(TEXT("既定のサンプル数が %d 本（16 本以内に収めたい）"), DefaultCount));
	}

	// 小数の点数は切り捨てて扱う
	TestEqual(TEXT("PickSampleOffsets(16, 4.9, 1) の個数"), Cubelith::PickSampleOffsets(16.0, 4.9, 1.0).Num(), 1 + 4);

	return !HasAnyErrors();
}

// 2. リングは内側から順に並び、i 本目の半径は Radius * i / Rings
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPickSamplesRadiusTest, "CUBELITH.Render.PickSamples.Radius",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPickSamplesRadiusTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	const TArray<Cubelith::FPickOffset> Offsets = Cubelith::PickSampleOffsets(12.0, 4.0, 2.0);
	if (Offsets.Num() != 9)
	{
		AddError(FString::Printf(TEXT("PickSampleOffsets(12, 4, 2) の個数が %d（期待 9）"), Offsets.Num()));
		return false;
	}

	// 中心を除いた 8 点のうち、前半 4 点が半径 6、後半 4 点が半径 12
	for (int32 Index = 1; Index <= 4; ++Index)
	{
		if (!FMath::IsNearlyEqual(OffsetLength(Offsets[Index]), 6.0, PickTolerance))
		{
			AddError(FString::Printf(TEXT("内側のリング %d 点目の半径が %f（期待 6）"),
				Index, OffsetLength(Offsets[Index])));
		}
	}
	for (int32 Index = 5; Index <= 8; ++Index)
	{
		if (!FMath::IsNearlyEqual(OffsetLength(Offsets[Index]), 12.0, PickTolerance))
		{
			AddError(FString::Printf(TEXT("外側のリング %d 点目の半径が %f（期待 12）"),
				Index, OffsetLength(Offsets[Index])));
		}
	}

	// 1 リングなら指定した半径の円周上に等間隔で並ぶ
	const TArray<Cubelith::FPickOffset> Single = Cubelith::PickSampleOffsets(10.0, 4.0, 1.0);
	if (Single.Num() != 5)
	{
		AddError(FString::Printf(TEXT("PickSampleOffsets(10, 4, 1) の個数が %d（期待 5）"), Single.Num()));
		return false;
	}

	const Cubelith::FPickOffset Expected[4] = {
		{ 10.0, 0.0 },
		{ 0.0, 10.0 },
		{ -10.0, 0.0 },
		{ 0.0, -10.0 },
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const Cubelith::FPickOffset& Actual = Single[Index + 1];
		if (!FMath::IsNearlyEqual(Actual.Dx, Expected[Index].Dx, PickTolerance)
			|| !FMath::IsNearlyEqual(Actual.Dy, Expected[Index].Dy, PickTolerance))
		{
			AddError(FString::Printf(TEXT("1 リングの %d 点目が %s（期待 %s）"),
				Index + 1, *Describe(Actual), *Describe(Expected[Index])));
		}
	}

	// リングごとに半ステップずらして方角が重ならない（内側 1 点目は 0 度、外側 1 点目は 45 度）
	if (!FMath::IsNearlyEqual(OffsetAngle(Offsets[1]), 0.0, PickTolerance))
	{
		AddError(FString::Printf(TEXT("内側 1 点目の角度が %f（期待 0）"), OffsetAngle(Offsets[1])));
	}
	if (!FMath::IsNearlyEqual(OffsetAngle(Offsets[5]), UE_DOUBLE_PI / 4.0, PickTolerance))
	{
		AddError(FString::Printf(TEXT("外側 1 点目の角度が %f（期待 %f）"),
			OffsetAngle(Offsets[5]), UE_DOUBLE_PI / 4.0));
	}

	return !HasAnyErrors();
}

// 3. 不正な引数では中心 1 点だけにフォールバックする
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPickSamplesFallbackTest, "CUBELITH.Render.PickSamples.Fallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPickSamplesFallbackTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	struct FCase
	{
		const TCHAR* Name;
		double Radius;
		double Points;
		double Rings;
	};

	const FCase Cases[] = {
		{ TEXT("半径 0"), 0.0, Cubelith::DefaultPickRingPoints, Cubelith::DefaultPickRingCount },
		{ TEXT("半径が負"), -4.0, Cubelith::DefaultPickRingPoints, Cubelith::DefaultPickRingCount },
		{ TEXT("半径が NaN"), PickNaN(), Cubelith::DefaultPickRingPoints, Cubelith::DefaultPickRingCount },
		{ TEXT("半径が無限"), PickInfinity(), Cubelith::DefaultPickRingPoints, Cubelith::DefaultPickRingCount },
		{ TEXT("点数 0"), 16.0, 0.0, 2.0 },
		{ TEXT("リング 0"), 16.0, 6.0, 0.0 },
		{ TEXT("点数が負"), 16.0, -3.0, 2.0 },
		{ TEXT("点数が NaN"), 16.0, PickNaN(), 2.0 },
		{ TEXT("リングが NaN"), 16.0, 6.0, PickNaN() },
	};

	for (const FCase& Case : Cases)
	{
		const TArray<Cubelith::FPickOffset> Offsets = Cubelith::PickSampleOffsets(Case.Radius, Case.Points, Case.Rings);
		if (Offsets.Num() != 1 || !IsCenter(Offsets[0]))
		{
			AddError(FString::Printf(TEXT("%s: 中心 1 点に落ちなかった（%d 点）"), Case.Name, Offsets.Num()));
		}
	}

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
