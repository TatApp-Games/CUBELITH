// CubelithCoords（ロジック座標 → UE 座標の変換。Docs/SPEC_UE.md 7.2）のテスト。
// 確かめるのは軸の入れ替え・ボクセルの大きさ・24 通りの向きが鏡像にならないこと・向きの合成との整合。
// 判定の要は RotationMatchesRotateVoxel で、R_UE = P · R · P と FMatrix の規約（転置の有無）を同時に固定している。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithCoords.h"
#include "Grid.h"

namespace CubelithRenderTests
{
	namespace
	{
		/** 位置・回転の比較に使う許容誤差。整数の 90 度回転しか扱わないので丸め誤差はこの程度に収まる */
		constexpr double Tolerance = KINDA_SMALL_NUMBER;

		bool NearlyEqual(const FVector& A, const FVector& B)
		{
			return A.Equals(B, Tolerance);
		}

		/** FMatrix::TransformVector は FVector4 を返すので、比較しやすい FVector に落とす */
		FVector RotateBy(const FMatrix& M, const FVector& V)
		{
			const FVector4 Rotated = M.TransformVector(V);
			return FVector(Rotated.X, Rotated.Y, Rotated.Z);
		}

		FString Describe(const Cubelith::FVec3& V)
		{
			return FString::Printf(TEXT("(%d, %d, %d)"), V.X, V.Y, V.Z);
		}

		/** 24 通りの向きを一通り確かめるときに使う、癖のあるボクセル座標の並び */
		const TArray<Cubelith::FVec3>& SampleVoxels()
		{
			static const TArray<Cubelith::FVec3> Voxels = {
				Cubelith::Vec3(0, 0, 0),
				Cubelith::Vec3(1, 0, 0),
				Cubelith::Vec3(0, 1, 0),
				Cubelith::Vec3(0, 0, 1),
				Cubelith::Vec3(1, 2, 3),
				Cubelith::Vec3(-1, 2, -3),
				Cubelith::Vec3(4, -5, 6),
			};
			return Voxels;
		}
	}
}

// 1. 軸の入れ替え: UE の (X, Y, Z) = (x, z, y) × 100
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithCoordsVoxelToWorldTest, "CUBELITH.Render.Coords.VoxelToWorld",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithCoordsVoxelToWorldTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	struct FCase
	{
		Cubelith::FVec3 Voxel;
		FVector Expected;
	};

	const FCase Cases[] = {
		{ Cubelith::Vec3(0, 0, 0), FVector(0.0, 0.0, 0.0) },
		{ Cubelith::Vec3(1, 2, 3), FVector(100.0, 300.0, 200.0) },
		{ Cubelith::Vec3(1, 0, 0), FVector(100.0, 0.0, 0.0) },
		// ロジックの y（上）は UE の Z、ロジックの z は UE の Y
		{ Cubelith::Vec3(0, 1, 0), FVector(0.0, 0.0, 100.0) },
		{ Cubelith::Vec3(0, 0, 1), FVector(0.0, 100.0, 0.0) },
		{ Cubelith::Vec3(-1, -2, -3), FVector(-100.0, -300.0, -200.0) },
		{ Cubelith::Vec3(-4, 5, -6), FVector(-400.0, -600.0, 500.0) },
	};

	for (const FCase& Case : Cases)
	{
		const FVector Actual = Cubelith::VoxelToWorld(Case.Voxel);
		if (!NearlyEqual(Actual, Case.Expected))
		{
			AddError(FString::Printf(TEXT("VoxelToWorld%s が違う: %s（期待 %s）"),
				*Describe(Case.Voxel), *Actual.ToString(), *Case.Expected.ToString()));
		}
	}

	return !HasAnyErrors();
}

// 2. ボクセルの大きさ: 隣り合うボクセルの距離が VoxelSizeCm
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithCoordsVoxelSizeTest, "CUBELITH.Render.Coords.VoxelSize",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithCoordsVoxelSizeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	TestEqual(TEXT("VoxelSizeCm は 100 cm"), Cubelith::VoxelSizeCm, 100.0);

	const Cubelith::FVec3 Neighbors[] = {
		Cubelith::Vec3(1, 0, 0),
		Cubelith::Vec3(-1, 0, 0),
		Cubelith::Vec3(0, 1, 0),
		Cubelith::Vec3(0, -1, 0),
		Cubelith::Vec3(0, 0, 1),
		Cubelith::Vec3(0, 0, -1),
	};

	for (const Cubelith::FVec3& Base : SampleVoxels())
	{
		const FVector BaseWorld = Cubelith::VoxelToWorld(Base);
		for (const Cubelith::FVec3& Offset : Neighbors)
		{
			const FVector NeighborWorld = Cubelith::VoxelToWorld(Cubelith::AddVec3(Base, Offset));
			const double Distance = FVector::Dist(BaseWorld, NeighborWorld);
			if (!FMath::IsNearlyEqual(Distance, Cubelith::VoxelSizeCm, Tolerance))
			{
				AddError(FString::Printf(TEXT("%s の隣 %s との距離が %f（期待 %f）"),
					*Describe(Base), *Describe(Offset), Distance, Cubelith::VoxelSizeCm));
			}
		}
	}

	return !HasAnyErrors();
}

// 3. R_UE = P · R · P の検証: ロジックで回してから写すのと、写してから UE で回すのが一致する
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithCoordsRotationMatchesRotateVoxelTest, "CUBELITH.Render.Coords.RotationMatchesRotateVoxel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithCoordsRotationMatchesRotateVoxelTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	for (int32 Orientation = 0; Orientation < Cubelith::OrientationCount; ++Orientation)
	{
		const FMatrix Matrix = Cubelith::OrientationToWorldMatrix(Orientation);
		const FQuat Quat = Cubelith::OrientationToWorldQuat(Orientation);

		for (const Cubelith::FVec3& Voxel : SampleVoxels())
		{
			// ロジックで回してから UE 座標へ写した結果（これが正）
			const FVector Expected = Cubelith::VoxelToWorld(Cubelith::RotateVoxel(Voxel, Orientation));

			const FVector World = Cubelith::VoxelToWorld(Voxel);
			const FVector ByMatrix = RotateBy(Matrix, World);
			const FVector ByQuat = Quat.RotateVector(World);

			if (!NearlyEqual(ByMatrix, Expected))
			{
				AddError(FString::Printf(TEXT("向き %d・ボクセル %s で行列の結果が違う: %s（期待 %s）"),
					Orientation, *Describe(Voxel), *ByMatrix.ToString(), *Expected.ToString()));
			}
			if (!NearlyEqual(ByQuat, Expected))
			{
				AddError(FString::Printf(TEXT("向き %d・ボクセル %s でクォータニオンの結果が違う: %s（期待 %s）"),
					Orientation, *Describe(Voxel), *ByQuat.ToString(), *Expected.ToString()));
			}
		}
	}

	return !HasAnyErrors();
}

// 4. 鏡像にならないこと: 24 通りすべてで行列式が +1（回転のみ・反転が入っていない）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithCoordsNoMirrorTest, "CUBELITH.Render.Coords.NoMirror",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithCoordsNoMirrorTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	for (int32 Orientation = 0; Orientation < Cubelith::OrientationCount; ++Orientation)
	{
		const FMatrix Matrix = Cubelith::OrientationToWorldMatrix(Orientation);

		const double Determinant = Matrix.Determinant();
		if (!FMath::IsNearlyEqual(Determinant, 1.0, Tolerance))
		{
			AddError(FString::Printf(TEXT("向き %d の行列式が %f（期待 +1。反転が入っている）"), Orientation, Determinant));
		}

		// 平行移動が入っていないこと（座標変換は VoxelToWorld の役目）
		if (!NearlyEqual(Matrix.GetOrigin(), FVector::ZeroVector))
		{
			AddError(FString::Printf(TEXT("向き %d の行列に平行移動が入っている: %s"),
				Orientation, *Matrix.GetOrigin().ToString()));
		}

		// スケールが 1 であること
		const FVector Scale = Matrix.GetScaleVector();
		if (!NearlyEqual(Scale, FVector::OneVector))
		{
			AddError(FString::Printf(TEXT("向き %d の行列のスケールが %s（期待 (1, 1, 1)）"),
				Orientation, *Scale.ToString()));
		}
	}

	return !HasAnyErrors();
}

// 5. 向きの合成との整合: ComposeOrientation(A, B)（A を適用してから B）と UE 側の合成が一致する
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithCoordsComposeTest, "CUBELITH.Render.Coords.Compose",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithCoordsComposeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	for (int32 A = 0; A < Cubelith::OrientationCount; ++A)
	{
		const FMatrix MatrixA = Cubelith::OrientationToWorldMatrix(A);
		const FQuat QuatA = Cubelith::OrientationToWorldQuat(A);

		for (int32 B = 0; B < Cubelith::OrientationCount; ++B)
		{
			const int32 Composed = Cubelith::ComposeOrientation(A, B);
			const FMatrix ComposedMatrix = Cubelith::OrientationToWorldMatrix(Composed);
			const FQuat ComposedQuat = Cubelith::OrientationToWorldQuat(Composed);

			const FMatrix MatrixB = Cubelith::OrientationToWorldMatrix(B);
			const FQuat QuatB = Cubelith::OrientationToWorldQuat(B);

			for (const Cubelith::FVec3& Voxel : SampleVoxels())
			{
				const FVector World = Cubelith::VoxelToWorld(Voxel);

				// 「A を適用してから B」を素直に 2 段で回した結果
				const FVector StepByStep = RotateBy(MatrixB, RotateBy(MatrixA, World));

				const FVector ByComposedMatrix = RotateBy(ComposedMatrix, World);
				if (!NearlyEqual(ByComposedMatrix, StepByStep))
				{
					AddError(FString::Printf(TEXT("合成 (%d, %d) → %d・ボクセル %s で行列が違う: %s（期待 %s）"),
						A, B, Composed, *Describe(Voxel),
						*ByComposedMatrix.ToString(), *StepByStep.ToString()));
				}
				const FVector ByComposedQuat = ComposedQuat.RotateVector(World);
				const FVector QuatStepByStep = QuatB.RotateVector(QuatA.RotateVector(World));
				if (!NearlyEqual(ByComposedQuat, QuatStepByStep))
				{
					AddError(FString::Printf(TEXT("合成 (%d, %d) → %d・ボクセル %s でクォータニオンが違う: %s（期待 %s）"),
						A, B, Composed, *Describe(Voxel),
						*ByComposedQuat.ToString(), *QuatStepByStep.ToString()));
				}
			}

			if (HasAnyErrors())
			{
				// 24 × 24 × 7 通りあるので、ずれたら最初の 1 組だけ報告して打ち切る
				return false;
			}
		}
	}

	return true;
}

// 6. 恒等の向きは恒等回転
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithCoordsIdentityTest, "CUBELITH.Render.Coords.Identity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithCoordsIdentityTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	const FMatrix Matrix = Cubelith::OrientationToWorldMatrix(Cubelith::IdentityOrientation);
	if (!Matrix.Equals(FMatrix::Identity, Tolerance))
	{
		AddError(FString::Printf(TEXT("IdentityOrientation の行列が恒等でない: %s"), *Matrix.ToString()));
	}

	const FQuat Quat = Cubelith::OrientationToWorldQuat(Cubelith::IdentityOrientation);
	if (!Quat.Equals(FQuat::Identity, Tolerance))
	{
		AddError(FString::Printf(TEXT("IdentityOrientation のクォータニオンが恒等でない: %s"), *Quat.ToString()));
	}

	for (const Cubelith::FVec3& Voxel : SampleVoxels())
	{
		const FVector World = Cubelith::VoxelToWorld(Voxel);
		if (!NearlyEqual(Quat.RotateVector(World), World))
		{
			AddError(FString::Printf(TEXT("IdentityOrientation でボクセル %s が動いた: %s"),
				*Describe(Voxel), *Quat.RotateVector(World).ToString()));
		}
	}

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
