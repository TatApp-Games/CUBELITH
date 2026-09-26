// 照合データ（Docs/FIXTURES.md）を読む共有ヘルパ。U1 の照合テストが使い回す
// テスト専用なので Private/Tests/ に置く（Json モジュールはテストのコードからだけ使う。Docs/SPEC_UE.md 7.1）

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Piece.h"

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

	/**
	 * JSON の「数値配列の配列」を int32 で読む（orientations.json の matrices のように行優先 9 要素の並びが続くもの）。
	 * 内側の要素数が ExpectedInnerNum でなければ失敗させる。
	 */
	bool ReadInt32ArrayOfArrays(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, int32 ExpectedInnerNum, TArray<TArray<int32>>& OutValues, FString& OutError);

	/**
	 * JSON の値が [x, y, z] の 3 要素配列なら FVec3 として読む（Docs/FIXTURES.md「共通の表し方」）。
	 * What はずれたときのメッセージに出す場所の名前（例 "pieces[0].voxels[2]"）。
	 */
	bool ReadVec3(const TSharedPtr<FJsonValue>& Value, const FString& What, Cubelith::FVec3& OutVec3, FString& OutError);

	/** Object の FieldName が [x, y, z] の配列（pieces[].voxels のような並び）なら TArray<FVec3> として読む */
	bool ReadVec3Array(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<Cubelith::FVec3>& OutVec3s, FString& OutError);

	/** JSON の値が { "pieceId", "orientation", "position" } なら FPlacement として読む（Docs/FIXTURES.md「共通の表し方」） */
	bool ReadPlacement(const TSharedPtr<FJsonValue>& Value, const FString& What, Cubelith::FPlacement& OutPlacement, FString& OutError);

	/** Object の FieldName が配置の配列（solution・scatter のような並び）なら TArray<FPlacement> として読む */
	bool ReadPlacementArray(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<Cubelith::FPlacement>& OutPlacements, FString& OutError);
}

#endif // WITH_DEV_AUTOMATION_TESTS
