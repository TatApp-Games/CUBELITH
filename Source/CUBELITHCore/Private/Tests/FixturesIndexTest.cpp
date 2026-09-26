// 照合データ（Docs/FIXTURES.md）の目録を読めることを確かめる。U1 の照合テストはこの読み方を土台にする

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CubelithCoreTests
{
	// 照合データの置き場所（Docs/SPEC_UE.md 7.3）
	static FString GetFixturesDir()
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/CUBELITHCore/Private/Tests/Fixtures"));
	}

	// 想定している照合データの形の版（Docs/FIXTURES.md の formatVersion）
	static constexpr int32 ExpectedFormatVersion = 1;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithFixturesIndexTest, "CUBELITH.Core.Fixtures.Index",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithFixturesIndexTest::RunTest(const FString& Parameters)
{
	const FString FixturesDir = CubelithCoreTests::GetFixturesDir();
	const FString IndexPath = FPaths::Combine(FixturesDir, TEXT("index.json"));

	FString IndexText;
	if (!FFileHelper::LoadFileToString(IndexText, *IndexPath))
	{
		AddError(FString::Printf(TEXT("index.json を読めない: %s"), *IndexPath));
		return false;
	}

	TSharedPtr<FJsonObject> Index;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(IndexText);
	if (!FJsonSerializer::Deserialize(Reader, Index) || !Index.IsValid())
	{
		AddError(TEXT("index.json が JSON として読めない"));
		return false;
	}

	// 形の版が想定と違えば、以降を読まずに失敗させる（Docs/FIXTURES.md）
	int32 FormatVersion = 0;
	if (!Index->TryGetNumberField(TEXT("formatVersion"), FormatVersion)
		|| !TestEqual(TEXT("formatVersion"), FormatVersion, CubelithCoreTests::ExpectedFormatVersion))
	{
		AddError(TEXT("照合データの形の版が想定と違う"));
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

	for (const TSharedPtr<FJsonValue>& File : *Files)
	{
		const FString FileName = File->AsString();
		TestTrue(FString::Printf(TEXT("%s がある"), *FileName), FPaths::FileExists(FPaths::Combine(FixturesDir, FileName)));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
