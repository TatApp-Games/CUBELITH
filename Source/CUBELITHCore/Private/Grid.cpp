#include "Grid.h"

namespace Cubelith
{
	namespace
	{
		// ---- ここから下は grid.ts のモジュール内部の実装（向きの表の構築）をそのまま移したもの ----

		const FMat3 IdentityMatrix = { { 1, 0, 0, 0, 1, 0, 0, 0, 1 } };

		// 各軸まわりの +90 度回転（右手系）
		const FMat3 RotationX = { { 1, 0, 0, 0, 0, -1, 0, 1, 0 } };
		const FMat3 RotationY = { { 0, 0, 1, 0, 1, 0, -1, 0, 0 } };
		const FMat3 RotationZ = { { 0, -1, 0, 1, 0, 0, 0, 0, 1 } };

		/** 行列積 A·B（TS の multiplyMat3） */
		FMat3 MultiplyMat3(const FMat3& A, const FMat3& B)
		{
			return FMat3{ {
				A.M[0] * B.M[0] + A.M[1] * B.M[3] + A.M[2] * B.M[6], A.M[0] * B.M[1] + A.M[1] * B.M[4] + A.M[2] * B.M[7], A.M[0] * B.M[2] + A.M[1] * B.M[5] + A.M[2] * B.M[8],
				A.M[3] * B.M[0] + A.M[4] * B.M[3] + A.M[5] * B.M[6], A.M[3] * B.M[1] + A.M[4] * B.M[4] + A.M[5] * B.M[7], A.M[3] * B.M[2] + A.M[4] * B.M[5] + A.M[5] * B.M[8],
				A.M[6] * B.M[0] + A.M[7] * B.M[3] + A.M[8] * B.M[6], A.M[6] * B.M[1] + A.M[7] * B.M[4] + A.M[8] * B.M[7], A.M[6] * B.M[2] + A.M[7] * B.M[5] + A.M[8] * B.M[8],
			} };
		}

		/** 転置。回転行列では逆回転にあたる（TS の transposeMat3） */
		FMat3 TransposeMat3(const FMat3& A)
		{
			return FMat3{ {
				A.M[0], A.M[3], A.M[6],
				A.M[1], A.M[4], A.M[7],
				A.M[2], A.M[5], A.M[8],
			} };
		}

		/** ずれたときのメッセージ用（TS の mat3Key はキーにも使っていたが、C++ では FMat3 をそのままキーにできる） */
		FString Mat3ToString(const FMat3& A)
		{
			FString Text;
			for (int32 Index = 0; Index < 9; ++Index)
			{
				if (Index > 0)
				{
					Text += TEXT(",");
				}
				Text += FString::FromInt(A.M[Index]);
			}
			return Text;
		}

		/**
		 * 恒等から 3 軸の 90 度回転で閉包を取り、24 個の回転行列を作る（TS の buildOrientationMatrices）。
		 * 幅優先で辿るので id 0 は必ず恒等になり、列挙順は決定的。生成元が回転のみなので鏡像は入らない。
		 * 生成元の順（X → Y → Z）と「generator * current」の向きが id の割り当てを決めるので、TS と一字一句合わせてある。
		 * 並びを決めるのは TArray で、TSet は重複の検出にだけ使う（Docs/SPEC_UE.md 7.1）。
		 */
		TArray<FMat3> BuildOrientationMatrices()
		{
			const FMat3 Generators[] = { RotationX, RotationY, RotationZ };

			TArray<FMat3> Matrices;
			Matrices.Add(IdentityMatrix);

			TSet<FMat3> Seen;
			Seen.Add(IdentityMatrix);

			for (int32 Head = 0; Head < Matrices.Num(); ++Head)
			{
				// Matrices はこのループの中で伸びるので、参照ではなく値で持つ
				const FMat3 Current = Matrices[Head];
				for (const FMat3& Generator : Generators)
				{
					const FMat3 Next = MultiplyMat3(Generator, Current);
					if (Seen.Contains(Next))
					{
						continue;
					}
					Seen.Add(Next);
					Matrices.Add(Next);
				}
			}

			checkf(Matrices.Num() == OrientationCount, TEXT("向きの列挙に失敗: %d 個になった"), Matrices.Num());
			// 幅優先なので id 0 は必ず恒等になる（照合データもこの前提で並んでいる）
			checkf(Matrices[IdentityOrientation] == IdentityMatrix, TEXT("向き %d が恒等になっていない"), IdentityOrientation);
			return Matrices;
		}

		/** 向きの表。関数内 static なので一度だけ作られる（C++11 以降は初期化がスレッド安全） */
		const TArray<FMat3>& GetOrientationMatrices()
		{
			static const TArray<FMat3> Matrices = BuildOrientationMatrices();
			return Matrices;
		}

		/** 回転行列 → 向き id の逆引き。引くだけで並びは見ないので TMap でよい（TS の ORIENTATION_BY_MATRIX） */
		const TMap<FMat3, int32>& GetOrientationByMatrix()
		{
			static const TMap<FMat3, int32> ByMatrix = []()
			{
				const TArray<FMat3>& Matrices = GetOrientationMatrices();
				TMap<FMat3, int32> Map;
				Map.Reserve(Matrices.Num());
				for (int32 Id = 0; Id < Matrices.Num(); ++Id)
				{
					Map.Add(Matrices[Id], Id);
				}
				return Map;
			}();
			return ByMatrix;
		}

		/** 回転行列 → 向き id。24 通りの外の行列は呼び出し側のバグ（TS は RangeError） */
		int32 OrientationIdOf(const FMat3& A)
		{
			const int32* Id = GetOrientationByMatrix().Find(A);
			checkf(Id != nullptr, TEXT("90 度単位の回転ではない行列: %s"), *Mat3ToString(A));
			return *Id;
		}

		/**
		 * 向き id が 0..23 であることを確かめてそのまま返す（TS の assertOrientation）。
		 * TS が RangeError を投げていた「呼び出し側のバグ」は checkf にする（移植の約束）。
		 * TS の「非整数の向き id」は C++ では int32 なので型で排除される。
		 */
		int32 AssertOrientation(int32 Orientation)
		{
			checkf(Orientation >= 0 && Orientation < OrientationCount,
				TEXT("向き id は 0..%d の整数 (got %d)"), OrientationCount - 1, Orientation);
			return Orientation;
		}

		/**
		 * 合成の表。Table[A * 24 + B] = 「A を適用してから B を適用した向き」（TS の buildComposeTable）。
		 * 行列では Mb·Ma の順。逆にすると別の表になる。
		 */
		const TArray<int32>& GetComposeTable()
		{
			static const TArray<int32> Table = []()
			{
				const TArray<FMat3>& Matrices = GetOrientationMatrices();
				TArray<int32> Result;
				Result.Reserve(OrientationCount * OrientationCount);
				for (int32 A = 0; A < OrientationCount; ++A)
				{
					for (int32 B = 0; B < OrientationCount; ++B)
					{
						Result.Add(OrientationIdOf(MultiplyMat3(Matrices[B], Matrices[A])));
					}
				}
				return Result;
			}();
			return Table;
		}

		/** 軸ごとの ±90 度回転に対応する向き id（TS の AXIS_ROTATION_ID） */
		struct FAxisRotation
		{
			int32 Plus = 0;
			int32 Minus = 0;
		};

		const FAxisRotation& GetAxisRotation(EAxis Axis)
		{
			// -90 度は +90 度の転置（逆回転）。EAxis の値 0 / 1 / 2 をそのまま添字にする
			static const FAxisRotation Rotations[3] = {
				{ OrientationIdOf(RotationX), OrientationIdOf(TransposeMat3(RotationX)) },
				{ OrientationIdOf(RotationY), OrientationIdOf(TransposeMat3(RotationY)) },
				{ OrientationIdOf(RotationZ), OrientationIdOf(TransposeMat3(RotationZ)) },
			};

			const int32 Index = static_cast<int32>(Axis);
			checkf(Index >= 0 && Index < 3, TEXT("回転軸が X / Y / Z のどれでもない (got %d)"), Index);
			return Rotations[Index];
		}
	}

	// ---- Vec3 のユーティリティ ----

	FVec3 Vec3(int32 X, int32 Y, int32 Z)
	{
		FVec3 Result;
		Result.X = X;
		Result.Y = Y;
		Result.Z = Z;
		return Result;
	}

	FVec3 AddVec3(const FVec3& A, const FVec3& B)
	{
		return Vec3(A.X + B.X, A.Y + B.Y, A.Z + B.Z);
	}

	FVec3 SubVec3(const FVec3& A, const FVec3& B)
	{
		return Vec3(A.X - B.X, A.Y - B.Y, A.Z - B.Z);
	}

	bool EqualsVec3(const FVec3& A, const FVec3& B)
	{
		return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
	}

	int32 CompareVec3(const FVec3& A, const FVec3& B)
	{
		// TS と同じく差をそのまま返す（負 / 0 / 正だけを見る）
		if (A.X != B.X)
		{
			return A.X - B.X;
		}
		if (A.Y != B.Y)
		{
			return A.Y - B.Y;
		}
		return A.Z - B.Z;
	}

	FString Vec3Key(const FVec3& V)
	{
		return FString::Printf(TEXT("%d,%d,%d"), V.X, V.Y, V.Z);
	}

	// ---- 向きの公開 API ----

	FMat3 OrientationMatrix(int32 Orientation)
	{
		return GetOrientationMatrices()[AssertOrientation(Orientation)];
	}

	FVec3 RotateVoxel(const FVec3& V, int32 Orientation)
	{
		const FMat3 M = OrientationMatrix(Orientation);
		return Vec3(
			M.M[0] * V.X + M.M[1] * V.Y + M.M[2] * V.Z,
			M.M[3] * V.X + M.M[4] * V.Y + M.M[5] * V.Z,
			M.M[6] * V.X + M.M[7] * V.Y + M.M[8] * V.Z);
	}

	int32 ComposeOrientation(int32 A, int32 B)
	{
		return GetComposeTable()[AssertOrientation(A) * OrientationCount + AssertOrientation(B)];
	}

	int32 RotateOrientation(int32 Orientation, EAxis Axis, int32 Dir)
	{
		// TS は dir を 1 | -1 の型で縛っていた。C++ では型で縛れないので checkf にする
		checkf(Dir == 1 || Dir == -1, TEXT("回転の向きは +1 / -1 (got %d)"), Dir);
		const FAxisRotation& Step = GetAxisRotation(Axis);
		return ComposeOrientation(Orientation, Dir == 1 ? Step.Plus : Step.Minus);
	}

	int32 NearestOrientation(TArrayView<const double> M)
	{
		// TS が RangeError を投げていた「呼び出し側のバグ」は checkf にする（移植の約束）
		checkf(M.Num() == 9, TEXT("NearestOrientation: 行優先の 9 要素が必要 (got %d)"), M.Num());
		for (int32 Index = 0; Index < M.Num(); ++Index)
		{
			checkf(FMath::IsFinite(M[Index]),
				TEXT("NearestOrientation: 有限の数値でない要素がある (%d 番目: %f)"), Index, M[Index]);
		}

		const TArray<FMat3>& Matrices = GetOrientationMatrices();
		int32 BestId = 0;
		double BestScore = TNumericLimits<double>::Lowest();
		for (int32 Id = 0; Id < OrientationCount; ++Id)
		{
			const FMat3& Candidate = Matrices[Id];
			double Score = 0.0;
			for (int32 Index = 0; Index < 9; ++Index)
			{
				Score += M[Index] * static_cast<double>(Candidate.M[Index]);
			}
			// 厳密な > なので同点は先に見た（＝ id の小さい）方が残る
			if (Score > BestScore)
			{
				BestScore = Score;
				BestId = Id;
			}
		}
		return BestId;
	}
}
