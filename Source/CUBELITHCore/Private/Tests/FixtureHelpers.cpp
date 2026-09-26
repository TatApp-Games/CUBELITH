#include "FixtureHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CubelithCoreTests
{
	namespace
	{
		// JSON の数値配列を int64 の並びとして取り出す。数値以外が混ざっていたら失敗させる
		bool ReadNumberArrayInternal(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<int64>& OutValues, FString& OutError)
		{
			OutValues.Reset();

			if (!Object.IsValid())
			{
				OutError = FString::Printf(TEXT("%s を読もうとしたが JSON が無効"), *FieldName);
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Object->TryGetArrayField(FieldName, Values) || Values == nullptr)
			{
				OutError = FString::Printf(TEXT("%s が配列として読めない"), *FieldName);
				return false;
			}

			OutValues.Reserve(Values->Num());
			for (int32 Index = 0; Index < Values->Num(); ++Index)
			{
				double Number = 0.0;
				if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetNumber(Number))
				{
					OutError = FString::Printf(TEXT("%s の %d 番目が数値でない"), *FieldName, Index);
					return false;
				}
				// 照合データの数値は整数だけ（Docs/FIXTURES.md）。double からの取りこぼしを避けて丸めてから整数にする
				OutValues.Add(FMath::RoundToInt64(Number));
			}

			return true;
		}
	}

	FString GetFixturesDir()
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/CUBELITHCore/Private/Tests/Fixtures"));
	}

	TSharedPtr<FJsonObject> LoadFixtureJson(const FString& FileName, FString& OutError)
	{
		const FString Path = FPaths::Combine(GetFixturesDir(), FileName);

		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("%s を読めない: %s"), *FileName, *Path);
			return nullptr;
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("%s が JSON として読めない"), *FileName);
			return nullptr;
		}

		return Root;
	}

	TSharedPtr<FJsonObject> LoadFixtureIndex(FString& OutError)
	{
		const TSharedPtr<FJsonObject> Index = LoadFixtureJson(TEXT("index.json"), OutError);
		if (!Index.IsValid())
		{
			return nullptr;
		}

		// 形の版が想定と違えば、以降を読まずに失敗させる（Docs/FIXTURES.md）
		int32 FormatVersion = 0;
		if (!Index->TryGetNumberField(TEXT("formatVersion"), FormatVersion))
		{
			OutError = TEXT("index.json に formatVersion が無い");
			return nullptr;
		}
		if (FormatVersion != ExpectedFormatVersion)
		{
			OutError = FString::Printf(TEXT("照合データの形の版が想定と違う: 期待 %d / 実際 %d"), ExpectedFormatVersion, FormatVersion);
			return nullptr;
		}

		return Index;
	}

	bool ReadNumberArray(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<int64>& OutValues, FString& OutError)
	{
		return ReadNumberArrayInternal(Object, FieldName, OutValues, OutError);
	}

	bool ReadUint32Array(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<uint32>& OutValues, FString& OutError)
	{
		OutValues.Reset();

		TArray<int64> Numbers;
		if (!ReadNumberArrayInternal(Object, FieldName, Numbers, OutError))
		{
			return false;
		}

		OutValues.Reserve(Numbers.Num());
		for (int32 Index = 0; Index < Numbers.Num(); ++Index)
		{
			const int64 Number = Numbers[Index];
			if (Number < 0 || Number > static_cast<int64>(MAX_uint32))
			{
				OutError = FString::Printf(TEXT("%s の %d 番目が uint32 に入らない: %lld"), *FieldName, Index, Number);
				return false;
			}
			OutValues.Add(static_cast<uint32>(Number));
		}

		return true;
	}

	bool ReadInt32ArrayOfArrays(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, int32 ExpectedInnerNum, TArray<TArray<int32>>& OutValues, FString& OutError)
	{
		OutValues.Reset();

		if (!Object.IsValid())
		{
			OutError = FString::Printf(TEXT("%s を読もうとしたが JSON が無効"), *FieldName);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Object->TryGetArrayField(FieldName, Rows) || Rows == nullptr)
		{
			OutError = FString::Printf(TEXT("%s が配列として読めない"), *FieldName);
			return false;
		}

		OutValues.Reserve(Rows->Num());
		for (int32 RowIndex = 0; RowIndex < Rows->Num(); ++RowIndex)
		{
			const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
			if (!(*Rows)[RowIndex].IsValid() || !(*Rows)[RowIndex]->TryGetArray(Row) || Row == nullptr)
			{
				OutError = FString::Printf(TEXT("%s の %d 番目が配列でない"), *FieldName, RowIndex);
				return false;
			}
			if (Row->Num() != ExpectedInnerNum)
			{
				OutError = FString::Printf(TEXT("%s の %d 番目の要素数が %d でない: %d"), *FieldName, RowIndex, ExpectedInnerNum, Row->Num());
				return false;
			}

			TArray<int32>& OutRow = OutValues.AddDefaulted_GetRef();
			OutRow.Reserve(Row->Num());
			for (int32 Index = 0; Index < Row->Num(); ++Index)
			{
				double Number = 0.0;
				if (!(*Row)[Index].IsValid() || !(*Row)[Index]->TryGetNumber(Number))
				{
					OutError = FString::Printf(TEXT("%s の %d 番目の %d 要素目が数値でない"), *FieldName, RowIndex, Index);
					return false;
				}
				// 照合データの数値は整数だけ（Docs/FIXTURES.md）。double からの取りこぼしを避けて丸めてから整数にする
				const int64 Value = FMath::RoundToInt64(Number);
				if (Value < static_cast<int64>(MIN_int32) || Value > static_cast<int64>(MAX_int32))
				{
					OutError = FString::Printf(TEXT("%s の %d 番目の %d 要素目が int32 に入らない: %lld"), *FieldName, RowIndex, Index, Value);
					return false;
				}
				OutRow.Add(static_cast<int32>(Value));
			}
		}

		return true;
	}

	bool ReadInt32Array(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<int32>& OutValues, FString& OutError)
	{
		OutValues.Reset();

		TArray<int64> Numbers;
		if (!ReadNumberArrayInternal(Object, FieldName, Numbers, OutError))
		{
			return false;
		}

		OutValues.Reserve(Numbers.Num());
		for (int32 Index = 0; Index < Numbers.Num(); ++Index)
		{
			const int64 Number = Numbers[Index];
			if (Number < static_cast<int64>(MIN_int32) || Number > static_cast<int64>(MAX_int32))
			{
				OutError = FString::Printf(TEXT("%s の %d 番目が int32 に入らない: %lld"), *FieldName, Index, Number);
				return false;
			}
			OutValues.Add(static_cast<int32>(Number));
		}

		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
