// 照合データ（Docs/FIXTURES.md）の目録を読めることを確かめる。U1 の照合テストはこの読み方を土台にする
// 読み込みは FixtureHelpers.h に切り出してある（他の照合テストと共通）

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FixtureHelpers.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithFixturesIndexTest, "CUBELITH.Core.Fixtures.Index",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithFixturesIndexTest::RunTest(const FString& Parameters)
{
	// 形の版が想定と違えば、以降を読まずに失敗させる（Docs/FIXTURES.md）
	FString Error;
	const TSharedPtr<FJsonObject> Index = CubelithCoreTests::LoadFixtureIndex(Error);
	if (!Index.IsValid())
	{
		AddError(Error);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
	if (!Index->TryGetArrayField(TEXT("files"), Files) || Files == nullptr)
	{
		AddError(TEXT("index.json に files が無い"));
		return false;
	}

	// orientations.json・rng.json・パズル 25 ファイル（Docs/FIXTURES.md の一覧から index.json 自身を除いた数）
	TestEqual(TEXT("files の件数"), Files->Num(), 27);

	const FString FixturesDir = CubelithCoreTests::GetFixturesDir();
	for (const TSharedPtr<FJsonValue>& File : *Files)
	{
		const FString FileName = File->AsString();
		TestTrue(FString::Printf(TEXT("%s がある"), *FileName), FPaths::FileExists(FPaths::Combine(FixturesDir, FileName)));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
