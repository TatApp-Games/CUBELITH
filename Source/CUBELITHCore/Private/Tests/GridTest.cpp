// WebMock/tests/grid.test.ts の移植と、照合データ Fixtures/orientations.json との一致の確認（Docs/FIXTURES.md の orientations.json の節）
// 移植しないテスト: 範囲外や非整数の向き id は RangeError（checkf で停止するため）
// 移植しないテスト: 要素数が 9 でなければ RangeError（checkf で停止するため）
// 移植しないテスト: 有限でない値を含むと RangeError（checkf で停止するため）
// このうち「非整数の向き id」は C++ では int32 なので型で排除される（checkf に残っているのは 0..23 の範囲の検査だけ）

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FixtureHelpers.h"
#include "Grid.h"
#include "Rng.h"

namespace CubelithCoreTests
{
	// GridTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace GridTestDetail
	{
		/** ずれたときに出すメッセージの数の上限（1 つの照合あたり）。全要素は比べたうえで、先頭の何件かだけ出す */
		constexpr int32 MaxReportedMismatches = 3;

		const Cubelith::EAxis Axes[3] = { Cubelith::EAxis::X, Cubelith::EAxis::Y, Cubelith::EAxis::Z };
		const TCHAR* const AxisNames[3] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
		const int32 Directions[2] = { 1, -1 };

		/** 3x3 行列式（テスト側で独立に計算する） */
		int32 Determinant(const Cubelith::FMat3& M)
		{
			return M.M[0] * (M.M[4] * M.M[8] - M.M[5] * M.M[7])
				- M.M[1] * (M.M[3] * M.M[8] - M.M[5] * M.M[6])
				+ M.M[2] * (M.M[3] * M.M[7] - M.M[4] * M.M[6]);
		}

		/** 行優先の 9 要素を "a,b,c,..." にする（ずれたときのメッセージ用） */
		FString Mat3ToString(const Cubelith::FMat3& M)
		{
			FString Text;
			for (int32 Index = 0; Index < 9; ++Index)
			{
				if (Index > 0)
				{
					Text += TEXT(",");
				}
				Text += FString::FromInt(M.M[Index]);
			}
			return Text;
		}

		/** 期待する座標と一致するか。成分を直に比べるので EqualsVec3 の実装には依存しない */
		bool CheckVec3(FAutomationTestBase& Test, const FString& What, const Cubelith::FVec3& Actual, const Cubelith::FVec3& Expected)
		{
			if (Actual.X == Expected.X && Actual.Y == Expected.Y && Actual.Z == Expected.Z)
			{
				return true;
			}
			Test.AddError(FString::Printf(TEXT("%s: 期待 (%d,%d,%d) / 実際 (%d,%d,%d)"),
				*What, Expected.X, Expected.Y, Expected.Z, Actual.X, Actual.Y, Actual.Z));
			return false;
		}

		/** ボクセル集合の全ペアの距離の 2 乗（昇順）。回転で不変なはず */
		TArray<int32> PairwiseSquaredDistances(const TArray<Cubelith::FVec3>& Voxels)
		{
			TArray<int32> Distances;
			for (int32 I = 0; I < Voxels.Num(); ++I)
			{
				for (int32 J = I + 1; J < Voxels.Num(); ++J)
				{
					const Cubelith::FVec3 D = Cubelith::SubVec3(Voxels[I], Voxels[J]);
					Distances.Add(D.X * D.X + D.Y * D.Y + D.Z * D.Z);
				}
			}
			Distances.Sort();
			return Distances;
		}

		/** 検証用のサンプルピース（L 字のテトロミノ）。対称性が低く回転の違いが出る */
		TArray<Cubelith::FVec3> SampleVoxels()
		{
			TArray<Cubelith::FVec3> Voxels;
			Voxels.Add(Cubelith::Vec3(0, 0, 0));
			Voxels.Add(Cubelith::Vec3(1, 0, 0));
			Voxels.Add(Cubelith::Vec3(2, 0, 0));
			Voxels.Add(Cubelith::Vec3(2, 1, 0));
			return Voxels;
		}

		/** Y 軸まわりに Degrees 度回した行列（行優先）。Grid.cpp の RotationY と同じ向きの回転 */
		TArray<double> RotationYMatrix(double Degrees)
		{
			const double Rad = Degrees * UE_DOUBLE_PI / 180.0;
			const double C = FMath::Cos(Rad);
			const double S = FMath::Sin(Rad);

			TArray<double> M;
			M.Reserve(9);
			M.Add(C);    M.Add(0.0); M.Add(S);
			M.Add(0.0);  M.Add(1.0); M.Add(0.0);
			M.Add(-S);   M.Add(0.0); M.Add(C);
			return M;
		}

		/** 向き id をそのまま行優先の 9 要素の double 配列にする（NearestOrientation に渡すため） */
		TArray<double> OrientationMatrixAsDoubles(int32 Orientation)
		{
			const Cubelith::FMat3 M = Cubelith::OrientationMatrix(Orientation);
			TArray<double> Values;
			Values.Reserve(9);
			for (int32 Index = 0; Index < 9; ++Index)
			{
				Values.Add(static_cast<double>(M.M[Index]));
			}
			return Values;
		}
	}
}

// ---- Vec3 のユーティリティ ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridVec3ComponentsTest, "CUBELITH.Core.Grid.Vec3Components",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridVec3ComponentsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	GridTestDetail::CheckVec3(*this, TEXT("Vec3(1, 2, 3)"), Cubelith::Vec3(1, 2, 3), Cubelith::Vec3(1, 2, 3));

	const Cubelith::FVec3 Sum = Cubelith::AddVec3(Cubelith::Vec3(1, 2, 3), Cubelith::Vec3(-4, 5, 6));
	GridTestDetail::CheckVec3(*this, TEXT("AddVec3"), Sum, Cubelith::Vec3(-3, 7, 9));

	const Cubelith::FVec3 Difference = Cubelith::SubVec3(Cubelith::Vec3(1, 2, 3), Cubelith::Vec3(-4, 5, 6));
	GridTestDetail::CheckVec3(*this, TEXT("SubVec3"), Difference, Cubelith::Vec3(5, -3, -3));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridVec3EqualsTest, "CUBELITH.Core.Grid.Vec3Equals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridVec3EqualsTest::RunTest(const FString& Parameters)
{
	// 全成分が一致したときだけ真
	TestTrue(TEXT("同じ座標"), Cubelith::EqualsVec3(Cubelith::Vec3(1, 2, 3), Cubelith::Vec3(1, 2, 3)));
	TestFalse(TEXT("Z が違う"), Cubelith::EqualsVec3(Cubelith::Vec3(1, 2, 3), Cubelith::Vec3(1, 2, 4)));
	TestFalse(TEXT("X が違う"), Cubelith::EqualsVec3(Cubelith::Vec3(1, 2, 3), Cubelith::Vec3(0, 2, 3)));
	TestFalse(TEXT("Y が違う"), Cubelith::EqualsVec3(Cubelith::Vec3(1, 2, 3), Cubelith::Vec3(1, 0, 3)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridVec3CompareTest, "CUBELITH.Core.Grid.Vec3Compare",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridVec3CompareTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// x → y → z の辞書順
	TestTrue(TEXT("X が小さい方が先"), Cubelith::CompareVec3(Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(1, 0, 0)) < 0);
	TestTrue(TEXT("X が同じなら Y を見る"), Cubelith::CompareVec3(Cubelith::Vec3(0, 5, 0), Cubelith::Vec3(0, 1, 9)) > 0);
	TestTrue(TEXT("X と Y が同じなら Z を見る"), Cubelith::CompareVec3(Cubelith::Vec3(0, 0, 2), Cubelith::Vec3(0, 0, 3)) < 0);
	TestTrue(TEXT("同じ座標は 0"), Cubelith::CompareVec3(Cubelith::Vec3(2, 3, 4), Cubelith::Vec3(2, 3, 4)) == 0);

	TArray<Cubelith::FVec3> Values;
	Values.Add(Cubelith::Vec3(1, 0, 0));
	Values.Add(Cubelith::Vec3(0, 0, 1));
	Values.Add(Cubelith::Vec3(0, 1, 0));
	Values.Add(Cubelith::Vec3(0, 0, 0));
	Values.Sort([](const Cubelith::FVec3& A, const Cubelith::FVec3& B) { return Cubelith::CompareVec3(A, B) < 0; });

	TArray<Cubelith::FVec3> Expected;
	Expected.Add(Cubelith::Vec3(0, 0, 0));
	Expected.Add(Cubelith::Vec3(0, 0, 1));
	Expected.Add(Cubelith::Vec3(0, 1, 0));
	Expected.Add(Cubelith::Vec3(1, 0, 0));

	for (int32 Index = 0; Index < Expected.Num(); ++Index)
	{
		GridTestDetail::CheckVec3(*this, FString::Printf(TEXT("並べ替えの %d 番目"), Index), Values[Index], Expected[Index]);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridVec3KeyTest, "CUBELITH.Core.Grid.Vec3Key",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridVec3KeyTest::RunTest(const FString& Parameters)
{
	// 座標が同じときだけ一致する
	TestEqual(TEXT("同じ座標は同じキー"), Cubelith::Vec3Key(Cubelith::Vec3(1, -2, 3)), Cubelith::Vec3Key(Cubelith::Vec3(1, -2, 3)));
	TestNotEqual(TEXT("符号が違えば別のキー"), Cubelith::Vec3Key(Cubelith::Vec3(1, -2, 3)), Cubelith::Vec3Key(Cubelith::Vec3(1, 2, 3)));

	TSet<FString> Keys;
	Keys.Add(Cubelith::Vec3Key(Cubelith::Vec3(1, 1, 1)));
	Keys.Add(Cubelith::Vec3Key(Cubelith::Vec3(1, 1, 1)));
	Keys.Add(Cubelith::Vec3Key(Cubelith::Vec3(1, 1, 2)));
	TestEqual(TEXT("キーで重複を落とせる"), Keys.Num(), 2);

	return true;
}

// ---- 向きの表 ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridOrientationBasicsTest, "CUBELITH.Core.Grid.OrientationBasics",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridOrientationBasicsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	TestEqual(TEXT("OrientationCount"), Cubelith::OrientationCount, 24);
	TestEqual(TEXT("IdentityOrientation"), Cubelith::IdentityOrientation, 0);

	const Cubelith::FMat3 Identity = Cubelith::OrientationMatrix(Cubelith::IdentityOrientation);
	const int32 Expected[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	for (int32 Index = 0; Index < 9; ++Index)
	{
		if (Identity.M[Index] != Expected[Index])
		{
			AddError(FString::Printf(TEXT("恒等の行列の %d 要素目が違う: 期待 %d / 実際 %d（全体 %s）"),
				Index, Expected[Index], Identity.M[Index], *GridTestDetail::Mat3ToString(Identity)));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridOrientationsDistinctTest, "CUBELITH.Core.Grid.OrientationsDistinct",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridOrientationsDistinctTest::RunTest(const FString& Parameters)
{
	// 24 通りの回転行列が相異なる
	TSet<Cubelith::FMat3> Matrices;
	for (int32 Id = 0; Id < Cubelith::OrientationCount; ++Id)
	{
		Matrices.Add(Cubelith::OrientationMatrix(Id));
	}
	TestEqual(TEXT("相異なる回転行列の数"), Matrices.Num(), Cubelith::OrientationCount);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridOrientationsDistinctByProbeTest, "CUBELITH.Core.Grid.OrientationsDistinctByProbe",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridOrientationsDistinctByProbeTest::RunTest(const FString& Parameters)
{
	// 各成分の絶対値が相異なる点なので、符号付き置換 24 通りは必ず別の点に写る
	const Cubelith::FVec3 Probe = Cubelith::Vec3(1, 2, 3);

	TSet<FString> Results;
	for (int32 Id = 0; Id < Cubelith::OrientationCount; ++Id)
	{
		Results.Add(Cubelith::Vec3Key(Cubelith::RotateVoxel(Probe, Id)));
	}
	TestEqual(TEXT("代表点の変換結果の数"), Results.Num(), Cubelith::OrientationCount);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridOrientationsDeterminantTest, "CUBELITH.Core.Grid.OrientationsDeterminant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridOrientationsDeterminantTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// すべて行列式 +1（鏡像を含まない）。要素が整数であることは int32 という型で保証される
	for (int32 Id = 0; Id < Cubelith::OrientationCount; ++Id)
	{
		const Cubelith::FMat3 M = Cubelith::OrientationMatrix(Id);
		const int32 Det = GridTestDetail::Determinant(M);
		if (Det != 1)
		{
			AddError(FString::Printf(TEXT("向き %d の行列式が +1 でない: %d（%s）"), Id, Det, *GridTestDetail::Mat3ToString(M)));
		}
	}

	return true;
}

// ---- RotateVoxel ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridRotateVoxelIdentityTest, "CUBELITH.Core.Grid.RotateVoxelIdentity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridRotateVoxelIdentityTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 恒等の向きでは座標が変わらない
	for (const Cubelith::FVec3& V : GridTestDetail::SampleVoxels())
	{
		const Cubelith::FVec3 Rotated = Cubelith::RotateVoxel(V, Cubelith::IdentityOrientation);
		GridTestDetail::CheckVec3(*this, FString::Printf(TEXT("恒等で (%s) を回す"), *Cubelith::Vec3Key(V)), Rotated, V);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridRotateVoxelAxisQuarterTest, "CUBELITH.Core.Grid.RotateVoxelAxisQuarter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridRotateVoxelAxisQuarterTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	const int32 X = Cubelith::RotateOrientation(Cubelith::IdentityOrientation, Cubelith::EAxis::X, 1);
	const int32 Y = Cubelith::RotateOrientation(Cubelith::IdentityOrientation, Cubelith::EAxis::Y, 1);
	const int32 Z = Cubelith::RotateOrientation(Cubelith::IdentityOrientation, Cubelith::EAxis::Z, 1);

	// Rx: (x, y, z) -> (x, -z, y)
	GridTestDetail::CheckVec3(*this, TEXT("X 軸 +90 度"), Cubelith::RotateVoxel(Cubelith::Vec3(1, 2, 3), X), Cubelith::Vec3(1, -3, 2));
	// Ry: (x, y, z) -> (z, y, -x)
	GridTestDetail::CheckVec3(*this, TEXT("Y 軸 +90 度"), Cubelith::RotateVoxel(Cubelith::Vec3(1, 2, 3), Y), Cubelith::Vec3(3, 2, -1));
	// Rz: (x, y, z) -> (-y, x, z)
	GridTestDetail::CheckVec3(*this, TEXT("Z 軸 +90 度"), Cubelith::RotateVoxel(Cubelith::Vec3(1, 2, 3), Z), Cubelith::Vec3(-2, 1, 3));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridRotateVoxelPreservesDistancesTest, "CUBELITH.Core.Grid.RotateVoxelPreservesDistances",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridRotateVoxelPreservesDistancesTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 整数座標のまま（int32 なので型で保証される）、ボクセル数と互いの距離を保つ
	const TArray<Cubelith::FVec3> Voxels = GridTestDetail::SampleVoxels();
	const TArray<int32> OriginalPairs = GridTestDetail::PairwiseSquaredDistances(Voxels);

	for (int32 Id = 0; Id < Cubelith::OrientationCount; ++Id)
	{
		TArray<Cubelith::FVec3> Rotated;
		Rotated.Reserve(Voxels.Num());
		for (const Cubelith::FVec3& V : Voxels)
		{
			Rotated.Add(Cubelith::RotateVoxel(V, Id));
		}

		TSet<Cubelith::FVec3> Unique(Rotated);
		if (Unique.Num() != Voxels.Num())
		{
			AddError(FString::Printf(TEXT("向き %d でボクセルが重なった: %d / %d"), Id, Unique.Num(), Voxels.Num()));
		}

		const TArray<int32> Pairs = GridTestDetail::PairwiseSquaredDistances(Rotated);
		if (Pairs.Num() != OriginalPairs.Num())
		{
			AddError(FString::Printf(TEXT("向き %d でペアの数が変わった: %d / %d"), Id, Pairs.Num(), OriginalPairs.Num()));
			continue;
		}
		for (int32 Index = 0; Index < Pairs.Num(); ++Index)
		{
			if (Pairs[Index] != OriginalPairs[Index])
			{
				AddError(FString::Printf(TEXT("向き %d で距離の 2 乗の %d 番目が変わった: 期待 %d / 実際 %d"),
					Id, Index, OriginalPairs[Index], Pairs[Index]));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridRotateVoxelOriginFixedTest, "CUBELITH.Core.Grid.RotateVoxelOriginFixed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridRotateVoxelOriginFixedTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 原点は動かない（回転はピースの局所原点まわり）
	for (int32 Id = 0; Id < Cubelith::OrientationCount; ++Id)
	{
		const Cubelith::FVec3 Rotated = Cubelith::RotateVoxel(Cubelith::Vec3(0, 0, 0), Id);
		GridTestDetail::CheckVec3(*this, FString::Printf(TEXT("向き %d で原点を回す"), Id), Rotated, Cubelith::Vec3(0, 0, 0));
	}

	return true;
}

// ---- 向きの合成 ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridComposeIdentityTest, "CUBELITH.Core.Grid.ComposeIdentity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridComposeIdentityTest::RunTest(const FString& Parameters)
{
	// 恒等の id が単位元として働く
	for (int32 Id = 0; Id < Cubelith::OrientationCount; ++Id)
	{
		const int32 Right = Cubelith::ComposeOrientation(Id, Cubelith::IdentityOrientation);
		const int32 Left = Cubelith::ComposeOrientation(Cubelith::IdentityOrientation, Id);
		if (Right != Id)
		{
			AddError(FString::Printf(TEXT("ComposeOrientation(%d, 恒等) が %d になった"), Id, Right));
		}
		if (Left != Id)
		{
			AddError(FString::Printf(TEXT("ComposeOrientation(恒等, %d) が %d になった"), Id, Left));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridComposeAssociativeTest, "CUBELITH.Core.Grid.ComposeAssociative",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridComposeAssociativeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 結合的である
	int32 MismatchCount = 0;
	for (int32 A = 0; A < Cubelith::OrientationCount; ++A)
	{
		for (int32 B = 0; B < Cubelith::OrientationCount; ++B)
		{
			for (int32 C = 0; C < Cubelith::OrientationCount; ++C)
			{
				const int32 Left = Cubelith::ComposeOrientation(Cubelith::ComposeOrientation(A, B), C);
				const int32 Right = Cubelith::ComposeOrientation(A, Cubelith::ComposeOrientation(B, C));
				if (Left != Right)
				{
					++MismatchCount;
					if (MismatchCount <= GridTestDetail::MaxReportedMismatches)
					{
						AddError(FString::Printf(TEXT("(%d, %d, %d) で結合的でない: (ab)c = %d / a(bc) = %d"), A, B, C, Left, Right));
					}
				}
			}
		}
	}
	if (MismatchCount > GridTestDetail::MaxReportedMismatches)
	{
		AddError(FString::Printf(TEXT("結合的でない組が %d 件あった"), MismatchCount));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridComposeMatchesStepwiseTest, "CUBELITH.Core.Grid.ComposeMatchesStepwise",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridComposeMatchesStepwiseTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 合成は「A を適用してから B を適用する」と一致する
	const Cubelith::FVec3 Probe = Cubelith::Vec3(1, 2, 3);
	int32 MismatchCount = 0;
	for (int32 A = 0; A < Cubelith::OrientationCount; ++A)
	{
		for (int32 B = 0; B < Cubelith::OrientationCount; ++B)
		{
			const Cubelith::FVec3 Composed = Cubelith::RotateVoxel(Probe, Cubelith::ComposeOrientation(A, B));
			const Cubelith::FVec3 Stepwise = Cubelith::RotateVoxel(Cubelith::RotateVoxel(Probe, A), B);
			if (!Cubelith::EqualsVec3(Composed, Stepwise))
			{
				++MismatchCount;
				if (MismatchCount <= GridTestDetail::MaxReportedMismatches)
				{
					AddError(FString::Printf(TEXT("(%d, %d) の合成が 2 段適用と違う: 合成 (%s) / 2 段 (%s)"),
						A, B, *Cubelith::Vec3Key(Composed), *Cubelith::Vec3Key(Stepwise)));
				}
			}
		}
	}
	if (MismatchCount > GridTestDetail::MaxReportedMismatches)
	{
		AddError(FString::Printf(TEXT("合成と 2 段適用が違う組が %d 件あった"), MismatchCount));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridComposeClosedTest, "CUBELITH.Core.Grid.ComposeClosed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridComposeClosedTest::RunTest(const FString& Parameters)
{
	// 群なので、1 つの A に対する合成結果は 24 通りを 1 度ずつ取る
	for (int32 A = 0; A < Cubelith::OrientationCount; ++A)
	{
		TSet<int32> Row;
		for (int32 B = 0; B < Cubelith::OrientationCount; ++B)
		{
			const int32 Id = Cubelith::ComposeOrientation(A, B);
			if (Id < 0 || Id >= Cubelith::OrientationCount)
			{
				AddError(FString::Printf(TEXT("ComposeOrientation(%d, %d) が 0..23 の外: %d"), A, B, Id));
			}
			Row.Add(Id);
		}
		if (Row.Num() != Cubelith::OrientationCount)
		{
			AddError(FString::Printf(TEXT("向き %d の合成結果が 24 通りを覆っていない: %d 通り"), A, Row.Num()));
		}
	}

	return true;
}

// ---- RotateOrientation ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridRotateOrientationFourTurnsTest, "CUBELITH.Core.Grid.RotateOrientationFourTurns",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridRotateOrientationFourTurnsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 同じ軸で 4 回 90 度回すと元の向きに戻る（全 24 向き × 3 軸 × ±）
	for (int32 Start = 0; Start < Cubelith::OrientationCount; ++Start)
	{
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			for (int32 DirIndex = 0; DirIndex < 2; ++DirIndex)
			{
				const Cubelith::EAxis Axis = GridTestDetail::Axes[AxisIndex];
				const int32 Dir = GridTestDetail::Directions[DirIndex];

				int32 Current = Start;
				for (int32 Step = 0; Step < 4; ++Step)
				{
					Current = Cubelith::RotateOrientation(Current, Axis, Dir);
					if (Step < 3 && Current == Start)
					{
						AddError(FString::Printf(TEXT("向き %d を %s 軸 %+d で %d 回回した時点で元に戻った"),
							Start, GridTestDetail::AxisNames[AxisIndex], Dir, Step + 1));
					}
				}
				if (Current != Start)
				{
					AddError(FString::Printf(TEXT("向き %d を %s 軸 %+d で 4 回回すと %d になった"),
						Start, GridTestDetail::AxisNames[AxisIndex], Dir, Current));
				}
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridRotateOrientationCancelsTest, "CUBELITH.Core.Grid.RotateOrientationCancels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridRotateOrientationCancelsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// +1 と -1 は互いに打ち消す
	for (int32 Start = 0; Start < Cubelith::OrientationCount; ++Start)
	{
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const Cubelith::EAxis Axis = GridTestDetail::Axes[AxisIndex];

			const int32 PlusThenMinus = Cubelith::RotateOrientation(Cubelith::RotateOrientation(Start, Axis, 1), Axis, -1);
			if (PlusThenMinus != Start)
			{
				AddError(FString::Printf(TEXT("向き %d を %s 軸で +1 → -1 したら %d になった"),
					Start, GridTestDetail::AxisNames[AxisIndex], PlusThenMinus));
			}

			const int32 MinusThenPlus = Cubelith::RotateOrientation(Cubelith::RotateOrientation(Start, Axis, -1), Axis, 1);
			if (MinusThenPlus != Start)
			{
				AddError(FString::Printf(TEXT("向き %d を %s 軸で -1 → +1 したら %d になった"),
					Start, GridTestDetail::AxisNames[AxisIndex], MinusThenPlus));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridRotateOrientationMinusIsThreePlusTest, "CUBELITH.Core.Grid.RotateOrientationMinusIsThreePlus",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridRotateOrientationMinusIsThreePlusTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// -1 は +1 を 3 回繰り返したものと同じ
	for (int32 Start = 0; Start < Cubelith::OrientationCount; ++Start)
	{
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const Cubelith::EAxis Axis = GridTestDetail::Axes[AxisIndex];

			int32 Thrice = Start;
			for (int32 Step = 0; Step < 3; ++Step)
			{
				Thrice = Cubelith::RotateOrientation(Thrice, Axis, 1);
			}

			const int32 Minus = Cubelith::RotateOrientation(Start, Axis, -1);
			if (Minus != Thrice)
			{
				AddError(FString::Printf(TEXT("向き %d の %s 軸 -1 が +1 の 3 回と違う: %d / %d"),
					Start, GridTestDetail::AxisNames[AxisIndex], Minus, Thrice));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridRotateOrientationReachesAllTest, "CUBELITH.Core.Grid.RotateOrientationReachesAll",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridRotateOrientationReachesAllTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 恒等から 3 軸の回転を組み合わせると 24 通りすべてに到達できる
	TSet<int32> Reached;
	Reached.Add(Cubelith::IdentityOrientation);

	TArray<int32> Queue;
	Queue.Add(Cubelith::IdentityOrientation);

	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 Current = Queue[Head];
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			for (int32 DirIndex = 0; DirIndex < 2; ++DirIndex)
			{
				const int32 Next = Cubelith::RotateOrientation(Current, GridTestDetail::Axes[AxisIndex], GridTestDetail::Directions[DirIndex]);
				if (Reached.Contains(Next))
				{
					continue;
				}
				Reached.Add(Next);
				Queue.Add(Next);
			}
		}
	}

	TestEqual(TEXT("到達できた向きの数"), Reached.Num(), Cubelith::OrientationCount);

	return true;
}

// ---- NearestOrientation ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridNearestExactTest, "CUBELITH.Core.Grid.NearestExact",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridNearestExactTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 24 通りの向き行列そのものを渡すと同じ id が返る
	for (int32 Id = 0; Id < Cubelith::OrientationCount; ++Id)
	{
		const TArray<double> M = GridTestDetail::OrientationMatrixAsDoubles(Id);
		const int32 Actual = Cubelith::NearestOrientation(M);
		if (Actual != Id)
		{
			AddError(FString::Printf(TEXT("向き %d の行列そのもので %d が返った"), Id, Actual));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridNearestWithNoiseTest, "CUBELITH.Core.Grid.NearestWithNoise",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridNearestWithNoiseTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 各要素に ±0.15 のノイズを載せても同じ id が返る。決定的な擬似乱数を使う（FMath::Rand は使わない）
	Cubelith::FRng Rng = Cubelith::CreateRng(20260906);
	int32 MismatchCount = 0;

	for (int32 Id = 0; Id < Cubelith::OrientationCount; ++Id)
	{
		const Cubelith::FMat3 Base = Cubelith::OrientationMatrix(Id);
		for (int32 Trial = 0; Trial < 20; ++Trial)
		{
			TArray<double> Noisy;
			Noisy.Reserve(9);
			for (int32 Index = 0; Index < 9; ++Index)
			{
				Noisy.Add(static_cast<double>(Base.M[Index]) + (Rng.Next() * 2.0 - 1.0) * 0.15);
			}

			const int32 Actual = Cubelith::NearestOrientation(Noisy);
			if (Actual != Id)
			{
				++MismatchCount;
				if (MismatchCount <= GridTestDetail::MaxReportedMismatches)
				{
					AddError(FString::Printf(TEXT("向き %d の %d 回目のノイズで %d が返った"), Id, Trial, Actual));
				}
			}
		}
	}
	if (MismatchCount > GridTestDetail::MaxReportedMismatches)
	{
		AddError(FString::Printf(TEXT("ノイズで別の向きになった試行が %d 件あった"), MismatchCount));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridNearestSmallRotationTest, "CUBELITH.Core.Grid.NearestSmallRotation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridNearestSmallRotationTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 恒等を少しだけ回した行列は IdentityOrientation になる
	TestEqual(TEXT("+20 度"), Cubelith::NearestOrientation(GridTestDetail::RotationYMatrix(20.0)), Cubelith::IdentityOrientation);
	TestEqual(TEXT("-20 度"), Cubelith::NearestOrientation(GridTestDetail::RotationYMatrix(-20.0)), Cubelith::IdentityOrientation);
	TestEqual(TEXT("0 度"), Cubelith::NearestOrientation(GridTestDetail::RotationYMatrix(0.0)), Cubelith::IdentityOrientation);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridNearestQuarterRotationTest, "CUBELITH.Core.Grid.NearestQuarterRotation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridNearestQuarterRotationTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 90 度に近い回転はその 90 度の向きになる
	const int32 Quarter = Cubelith::NearestOrientation(GridTestDetail::RotationYMatrix(90.0));
	TestEqual(TEXT("80 度"), Cubelith::NearestOrientation(GridTestDetail::RotationYMatrix(80.0)), Quarter);
	TestEqual(TEXT("100 度"), Cubelith::NearestOrientation(GridTestDetail::RotationYMatrix(100.0)), Quarter);
	// TestNotEqual には int32 の版が無いので TestTrue で見る
	TestTrue(TEXT("90 度は恒等ではない"), Quarter != Cubelith::IdentityOrientation);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridNearestZeroMatrixTest, "CUBELITH.Core.Grid.NearestZeroMatrix",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridNearestZeroMatrixTest::RunTest(const FString& Parameters)
{
	// 同点なら id の小さい方を返す（零行列はすべて内積 0）
	TArray<double> Zero;
	Zero.Init(0.0, 9);
	TestEqual(TEXT("零行列"), Cubelith::NearestOrientation(Zero), 0);

	return true;
}

// ---- 照合データ（Fixtures/orientations.json）との一致 ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithGridFixturesTest, "CUBELITH.Core.Grid.Fixtures",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithGridFixturesTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 期待値は C++ のリテラルで書き写さず、必ず JSON から読む（Docs/FIXTURES.md）
	FString Error;
	const TSharedPtr<FJsonObject> Orientations = LoadFixtureJson(TEXT("orientations.json"), Error);
	if (!Orientations.IsValid())
	{
		AddError(Error);
		return false;
	}

	int32 Count = 0;
	if (!Orientations->TryGetNumberField(TEXT("count"), Count))
	{
		AddError(TEXT("orientations.json に count が無い"));
		return false;
	}
	TestEqual(TEXT("count"), Count, Cubelith::OrientationCount);

	// matrices[id] は行優先の 9 要素（要素は -1 / 0 / 1）
	TArray<TArray<int32>> Matrices;
	if (!ReadInt32ArrayOfArrays(Orientations, TEXT("matrices"), 9, Matrices, Error))
	{
		AddError(Error);
		return false;
	}
	if (!TestEqual(TEXT("matrices の件数"), Matrices.Num(), Cubelith::OrientationCount))
	{
		return false;
	}

	int32 MatrixMismatchCount = 0;
	for (int32 Id = 0; Id < Matrices.Num(); ++Id)
	{
		const Cubelith::FMat3 Actual = Cubelith::OrientationMatrix(Id);
		for (int32 Index = 0; Index < 9; ++Index)
		{
			if (Actual.M[Index] != Matrices[Id][Index])
			{
				++MatrixMismatchCount;
				if (MatrixMismatchCount <= GridTestDetail::MaxReportedMismatches)
				{
					AddError(FString::Printf(TEXT("matrices[%d] の %d 要素目が違う: 期待 %d / 実際 %d（実際の行列 %s）"),
						Id, Index, Matrices[Id][Index], Actual.M[Index], *GridTestDetail::Mat3ToString(Actual)));
				}
			}
		}
	}
	if (MatrixMismatchCount > GridTestDetail::MaxReportedMismatches)
	{
		AddError(FString::Printf(TEXT("matrices は %d / %d 要素が違う（閉包を取る順が違う合図。Docs/FIXTURES.md）"),
			MatrixMismatchCount, Cubelith::OrientationCount * 9));
	}

	// compose[a * 24 + b] は「a を適用してから b」の向き id
	TArray<int32> Compose;
	if (!ReadInt32Array(Orientations, TEXT("compose"), Compose, Error))
	{
		AddError(Error);
		return false;
	}
	if (!TestEqual(TEXT("compose の件数"), Compose.Num(), Cubelith::OrientationCount * Cubelith::OrientationCount))
	{
		return false;
	}

	int32 ComposeMismatchCount = 0;
	for (int32 A = 0; A < Cubelith::OrientationCount; ++A)
	{
		for (int32 B = 0; B < Cubelith::OrientationCount; ++B)
		{
			const int32 Expected = Compose[A * Cubelith::OrientationCount + B];
			const int32 Actual = Cubelith::ComposeOrientation(A, B);
			if (Actual != Expected)
			{
				++ComposeMismatchCount;
				if (ComposeMismatchCount <= GridTestDetail::MaxReportedMismatches)
				{
					AddError(FString::Printf(TEXT("compose[%d * 24 + %d] が違う: 期待 %d / 実際 %d"), A, B, Expected, Actual));
				}
			}
		}
	}
	if (ComposeMismatchCount > GridTestDetail::MaxReportedMismatches)
	{
		AddError(FString::Printf(TEXT("compose は %d / %d 要素が違う（合成の向きが違う合図。Docs/FIXTURES.md）"),
			ComposeMismatchCount, Compose.Num()));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
