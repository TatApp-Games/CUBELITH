// 照合データ（Docs/FIXTURES.md）を読む共有ヘルパ。U1 の照合テストが使い回す
// テスト専用なので Private/Tests/ に置く（Json モジュールはテストのコードからだけ使う。Docs/SPEC_UE.md 7.1）

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"

namespace CubelithCoreTests
{
	/** 想定している照合データの形の版（Docs/FIXTURES.md の formatVersion） */
	inline constexpr int32 ExpectedFormatVersion = 1;

	/** 照合データの置き場所（Docs/SPEC_UE.md 7.3） */
	FString GetFixturesDir();

	/** 照合データの 1 ファイルを JSON として読む。読めなければ OutError に理由を入れて無効な値を返す */
	TSharedPtr<FJsonObject> LoadFixtureJson(const FString& FileName, FString& OutError);

	/** index.json を読み、formatVersion が想定どおりであることまで確かめる */
	TSharedPtr<FJsonObject> LoadFixtureIndex(FString& OutError);

	/** JSON の数値配列を int64 で読む（JSON の数値は double 経由で来るので、いちばん広い整数で受ける） */
	bool ReadNumberArray(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<int64>& OutValues, FString& OutError);

	/** JSON の数値配列を uint32 で読む（4294967295 のような int32 に入らない値を桁を落とさずに通す） */
	bool ReadUint32Array(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<uint32>& OutValues, FString& OutError);

	/** JSON の数値配列を int32 で読む */
	bool ReadInt32Array(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<int32>& OutValues, FString& OutError);
}

#endif // WITH_DEV_AUTOMATION_TESTS
