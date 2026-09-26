// ボクセル座標と 90 度単位の向き（24 通り）。移植元は WebMock/src/core/grid.ts（RULES.md 3.3）
// 座標は整数 (x, y, z) で Y が上。向きは行列式 +1 の整数回転行列 24 個に id 0..23 を振って表す
// 命名は 001 が決めた約束に揃える（namespace Cubelith・型は F 接頭辞・列挙は E 接頭辞・関数は WebMock と同じ名前を PascalCase に）

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

namespace Cubelith
{
	/**
	 * 整数のボクセル座標。Y が上（TS の Vec3）。
	 * FIntVector の別名にする手もあるが、WebMock との対応を見やすくするため専用の型にした（名前は FVec3 のまま）。
	 * operator== と GetTypeHash があるので TSet / TMap のキーにできる。
	 */
	struct CUBELITHCORE_API FVec3
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Z = 0;
	};

	inline bool operator==(const FVec3& A, const FVec3& B)
	{
		return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
	}

	inline bool operator!=(const FVec3& A, const FVec3& B)
	{
		return !(A == B);
	}

	/** TSet / TMap のキーにするためのハッシュ（ADL で見つかるよう namespace Cubelith に置く） */
	inline uint32 GetTypeHash(const FVec3& V)
	{
		// 素の GetTypeHash(int32) はグローバルにあるので :: を付けて呼ぶ（付けないとこの関数自身が見つかって型が合わない）
		return HashCombine(HashCombine(::GetTypeHash(V.X), ::GetTypeHash(V.Y)), ::GetTypeHash(V.Z));
	}

	/** 行優先の 3x3 整数行列（9 要素）。列ベクトルに左から掛ける（v' = M v）。TS の Mat3 */
	struct CUBELITHCORE_API FMat3
	{
		/** 行優先の 9 要素（M[Row * 3 + Col]）。既定は恒等 */
		int32 M[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

		int32 operator[](int32 Index) const
		{
			checkf(Index >= 0 && Index < 9, TEXT("FMat3 の添字は 0..8 (got %d)"), Index);
			return M[Index];
		}
	};

	inline bool operator==(const FMat3& A, const FMat3& B)
	{
		for (int32 Index = 0; Index < 9; ++Index)
		{
			if (A.M[Index] != B.M[Index])
			{
				return false;
			}
		}
		return true;
	}

	inline bool operator!=(const FMat3& A, const FMat3& B)
	{
		return !(A == B);
	}

	/** 向きの表を作るときの重複検出（TSet<FMat3>）に使うハッシュ */
	inline uint32 GetTypeHash(const FMat3& A)
	{
		uint32 Hash = ::GetTypeHash(A.M[0]);
		for (int32 Index = 1; Index < 9; ++Index)
		{
			Hash = HashCombine(Hash, ::GetTypeHash(A.M[Index]));
		}
		return Hash;
	}

	/**
	 * 回転軸。UI の Pitch = X / Yaw = Y / Roll = Z に対応する（TS の Axis）。
	 * UE Core にも EAxis があるが、あちらは名前空間 EAxis の中の enum（EAxis::Type）で別物。
	 * 名前としては衝突しない（この enum は Cubelith の中）ので WebMock と揃えて EAxis にしたが、
	 * namespace の外からは必ず Cubelith::EAxis と書く（using namespace Cubelith で取り込むと曖昧になる）。
	 */
	enum class EAxis : uint8
	{
		X = 0,
		Y = 1,
		Z = 2,
	};

	/** 90 度単位の向きの総数（TS の ORIENTATION_COUNT） */
	inline constexpr int32 OrientationCount = 24;

	/** 恒等（無回転）の向き id（TS の IDENTITY_ORIENTATION） */
	inline constexpr int32 IdentityOrientation = 0;

	/** FVec3 を作る（TS の vec3） */
	CUBELITHCORE_API FVec3 Vec3(int32 X, int32 Y, int32 Z);

	/** 和 A + B（TS の addVec3） */
	CUBELITHCORE_API FVec3 AddVec3(const FVec3& A, const FVec3& B);

	/** 差 A - B（TS の subVec3） */
	CUBELITHCORE_API FVec3 SubVec3(const FVec3& A, const FVec3& B);

	/** 座標が等しいか（TS の equalsVec3） */
	CUBELITHCORE_API bool EqualsVec3(const FVec3& A, const FVec3& B);

	/** 辞書順（X → Y → Z）の比較。負 / 0 / 正を返す。並びを決定的にするために使う（TS の compareVec3） */
	CUBELITHCORE_API int32 CompareVec3(const FVec3& A, const FVec3& B);

	/**
	 * "x,y,z" の文字列（TS の vec3Key）。
	 * TS では Set / Map のキー用だが、C++ では TSet<FVec3> が使えるのでロジックでは使わない。
	 * WebMock の公開関数に対応する関数を揃えるため移植してある（ずれたときのメッセージには便利）。
	 */
	CUBELITHCORE_API FString Vec3Key(const FVec3& V);

	/** 向き id → 3x3 の整数回転行列（行列式 +1）。TS の orientationMatrix */
	CUBELITHCORE_API FMat3 OrientationMatrix(int32 Orientation);

	/** 座標に向きを適用する。整数座標は整数座標のまま（TS の rotateVoxel） */
	CUBELITHCORE_API FVec3 RotateVoxel(const FVec3& V, int32 Orientation);

	/** 向き A を適用してから B を適用した向き（行列では Mb·Ma）。表引きで返す（TS の composeOrientation） */
	CUBELITHCORE_API int32 ComposeOrientation(int32 A, int32 B);

	/**
	 * 現在の向きに、ワールドの軸まわりの 90 度回転を 1 段重ねた向きを返す（TS の rotateOrientation）。
	 * RULES.md 3.3 の Pitch / Yaw / Roll の 90 度回転。Dir は +1 / -1。
	 */
	CUBELITHCORE_API int32 RotateOrientation(int32 Orientation, EAxis Axis, int32 Dir);

	/**
	 * 任意の 3x3 実数行列に最も近い 90 度単位の向き id を返す（TS の nearestOrientation）。
	 *
	 * 24 個の向き行列との Frobenius 内積（対応する要素どうしの積の和）が最大のものを選ぶ。
	 * 同点なら id の小さい方を返し、結果を決定的にする（更新は厳密な > で行う）。
	 * 引数は行優先の 9 要素（呼び出し側が FMatrix / FQuat から取り出して渡す想定なので、UE の行列型に依存しない）。
	 */
	CUBELITHCORE_API int32 NearestOrientation(TArrayView<const double> M);
}
