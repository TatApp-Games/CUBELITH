// WebMock/tests/rng.test.ts の移植と、照合データ Fixtures/rng.json との一致の確認（Docs/FIXTURES.md の rng.json の節）
// 移植しないテスト: nextInt は [0, n) の整数を返し、不正な n は例外（checkf で停止するため。[0, n) の整数を返す部分だけ移した）

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FixtureHelpers.h"
#include "Rng.h"

namespace CubelithCoreTests
{
	namespace
	{
		// ずれたときに出すメッセージの数の上限（1 列あたり）。全要素は比べたうえで、先頭の何件かだけ出す
		constexpr int32 MaxReportedMismatches = 3;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithRngSameSeedTest, "CUBELITH.Core.Rng.SameSeed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithRngSameSeedTest::RunTest(const FString& Parameters)
{
	Cubelith::FRng A = Cubelith::CreateRng(12345);
	Cubelith::FRng B = Cubelith::CreateRng(12345);

	for (int32 Index = 0; Index < 10; ++Index)
	{
		const double ValueA = A.Next();
		const double ValueB = B.Next();
		if (ValueA != ValueB)
		{
			AddError(FString::Printf(TEXT("%d 番目が違う: %f / %f"), Index, ValueA, ValueB));
			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithRngRangeTest, "CUBELITH.Core.Rng.Range",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithRngRangeTest::RunTest(const FString& Parameters)
{
	Cubelith::FRng Rng = Cubelith::CreateRng(7);

	for (int32 Index = 0; Index < 1000; ++Index)
	{
		const double Value = Rng.Next();
		if (Value < 0.0 || Value >= 1.0)
		{
			AddError(FString::Printf(TEXT("%d 番目が [0, 1) に収まらない: %f"), Index, Value));
			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithRngNextIntTest, "CUBELITH.Core.Rng.NextInt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithRngNextIntTest::RunTest(const FString& Parameters)
{
	Cubelith::FRng Rng = Cubelith::CreateRng(99);

	for (int32 Index = 0; Index < 1000; ++Index)
	{
		const int32 Value = Rng.NextInt(6);
		if (Value < 0 || Value >= 6)
		{
			AddError(FString::Printf(TEXT("%d 番目が [0, 6) に収まらない: %d"), Index, Value));
			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithRngFixturesTest, "CUBELITH.Core.Rng.Fixtures",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithRngFixturesTest::RunTest(const FString& Parameters)
{
	FString Error;

	// index.json の rngSeeds と rngStreamLength が、rng.json の並びと長さの正（Docs/FIXTURES.md）
	const TSharedPtr<FJsonObject> Index = CubelithCoreTests::LoadFixtureIndex(Error);
	if (!Index.IsValid())
	{
		AddError(Error);
		return false;
	}

	TArray<uint32> Seeds;
	if (!CubelithCoreTests::ReadUint32Array(Index, TEXT("rngSeeds"), Seeds, Error))
	{
		AddError(Error);
		return false;
	}

	int32 IndexStreamLength = 0;
	if (!Index->TryGetNumberField(TEXT("rngStreamLength"), IndexStreamLength))
	{
		AddError(TEXT("index.json に rngStreamLength が無い"));
		return false;
	}

	const TSharedPtr<FJsonObject> Rng = CubelithCoreTests::LoadFixtureJson(TEXT("rng.json"), Error);
	if (!Rng.IsValid())
	{
		AddError(Error);
		return false;
	}

	int32 StreamLength = 0;
	if (!Rng->TryGetNumberField(TEXT("streamLength"), StreamLength))
	{
		AddError(TEXT("rng.json に streamLength が無い"));
		return false;
	}
	if (!TestEqual(TEXT("streamLength が index.json の rngStreamLength と同じ"), StreamLength, IndexStreamLength))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Streams = nullptr;
	if (!Rng->TryGetArrayField(TEXT("streams"), Streams) || Streams == nullptr)
	{
		AddError(TEXT("rng.json に streams が無い"));
		return false;
	}
	if (!TestEqual(TEXT("streams の件数が rngSeeds と同じ"), Streams->Num(), Seeds.Num()))
	{
		return false;
	}

	for (int32 StreamIndex = 0; StreamIndex < Streams->Num(); ++StreamIndex)
	{
		const TSharedPtr<FJsonObject>* StreamPtr = nullptr;
		if (!(*Streams)[StreamIndex].IsValid() || !(*Streams)[StreamIndex]->TryGetObject(StreamPtr) || StreamPtr == nullptr)
		{
			AddError(FString::Printf(TEXT("streams の %d 番目が object でない"), StreamIndex));
			return false;
		}
		const TSharedPtr<FJsonObject>& Stream = *StreamPtr;

		// seed は int32 に入らない値（4294967295）が来るので double 経由で uint32 へ落とす
		double SeedNumber = 0.0;
		if (!Stream->TryGetNumberField(TEXT("seed"), SeedNumber))
		{
			AddError(FString::Printf(TEXT("streams の %d 番目に seed が無い"), StreamIndex));
			return false;
		}
		const int64 SeedValue = FMath::RoundToInt64(SeedNumber);
		if (SeedValue < 0 || SeedValue > static_cast<int64>(MAX_uint32))
		{
			AddError(FString::Printf(TEXT("streams の %d 番目の seed が uint32 に入らない: %lld"), StreamIndex, SeedValue));
			return false;
		}
		const uint32 Seed = static_cast<uint32>(SeedValue);

		// streams は index.json の rngSeeds の順
		if (Seed != Seeds[StreamIndex])
		{
			AddError(FString::Printf(TEXT("streams の %d 番目の seed が rngSeeds と違う: 期待 %u / 実際 %u"), StreamIndex, Seeds[StreamIndex], Seed));
			return false;
		}

		// 3 つの列はそれぞれ新しい CreateRng(seed) から取る（互いに乱数を食い合わせない。Docs/FIXTURES.md）
		TArray<uint32> ExpectedUint32;
		if (!CubelithCoreTests::ReadUint32Array(Stream, TEXT("uint32"), ExpectedUint32, Error))
		{
			AddError(FString::Printf(TEXT("seed %u: %s"), Seed, *Error));
			return false;
		}
		TestEqual(FString::Printf(TEXT("seed %u の uint32 の長さ"), Seed), ExpectedUint32.Num(), StreamLength);

		{
			Cubelith::FRng Generator = Cubelith::CreateRng(Seed);
			int32 MismatchCount = 0;
			for (int32 Element = 0; Element < ExpectedUint32.Num(); ++Element)
			{
				const uint32 Actual = Generator.NextUint32();
				if (Actual != ExpectedUint32[Element])
				{
					++MismatchCount;
					if (MismatchCount <= CubelithCoreTests::MaxReportedMismatches)
					{
						AddError(FString::Printf(TEXT("seed %u の uint32 の %d 番目が違う: 期待 %u / 実際 %u"),
							Seed, Element, ExpectedUint32[Element], Actual));
					}
				}
			}
			if (MismatchCount > CubelithCoreTests::MaxReportedMismatches)
			{
				AddError(FString::Printf(TEXT("seed %u の uint32 は %d / %d 要素が違う"), Seed, MismatchCount, ExpectedUint32.Num()));
			}
		}

		// nextInt24 / nextInt1000 も列ごとに乱数を作り直して比べる
		const int32 Divisors[] = { 24, 1000 };
		const TCHAR* ColumnNames[] = { TEXT("nextInt24"), TEXT("nextInt1000") };
		for (int32 Column = 0; Column < UE_ARRAY_COUNT(Divisors); ++Column)
		{
			TArray<int32> Expected;
			if (!CubelithCoreTests::ReadInt32Array(Stream, ColumnNames[Column], Expected, Error))
			{
				AddError(FString::Printf(TEXT("seed %u: %s"), Seed, *Error));
				return false;
			}
			TestEqual(FString::Printf(TEXT("seed %u の %s の長さ"), Seed, ColumnNames[Column]), Expected.Num(), StreamLength);

			Cubelith::FRng Generator = Cubelith::CreateRng(Seed);
			int32 MismatchCount = 0;
			for (int32 Element = 0; Element < Expected.Num(); ++Element)
			{
				const int32 Actual = Generator.NextInt(Divisors[Column]);
				if (Actual != Expected[Element])
				{
					++MismatchCount;
					if (MismatchCount <= CubelithCoreTests::MaxReportedMismatches)
					{
						AddError(FString::Printf(TEXT("seed %u の %s の %d 番目が違う: 期待 %d / 実際 %d"),
							Seed, ColumnNames[Column], Element, Expected[Element], Actual));
					}
				}
			}
			if (MismatchCount > CubelithCoreTests::MaxReportedMismatches)
			{
				AddError(FString::Printf(TEXT("seed %u の %s は %d / %d 要素が違う"), Seed, ColumnNames[Column], MismatchCount, Expected.Num()));
			}
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
