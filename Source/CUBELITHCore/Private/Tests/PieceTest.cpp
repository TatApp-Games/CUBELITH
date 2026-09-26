// WebMock/tests/piece.test.ts の正常系の移植と、照合データから座標・配置を読めることの確認（Docs/FIXTURES.md「共通の表し方」）
// 移植しないテスト: localOrigin の「空のボクセル集合は RangeError」（checkf で停止するため）
// 移植しないテスト: normalizePiece / createPiece の「空のボクセル集合は RangeError」（checkf で停止するため）
// 移植しないテスト: boundingBox の「空のボクセル集合は RangeError」（checkf で停止するため）
// 移植しないテスト: placedVoxels の「ピース id と配置の id が食い違ったら例外」（checkf で停止するため）

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "FixtureHelpers.h"
#include "Grid.h"
#include "Piece.h"

namespace CubelithCoreTests
{
	// PieceTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace PieceTestDetail
	{
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

		/** ボクセルの並びが期待どおりか（個数と、並び順を含めた各要素） */
		bool CheckVoxels(FAutomationTestBase& Test, const FString& What, TArrayView<const Cubelith::FVec3> Actual, TArrayView<const Cubelith::FVec3> Expected)
		{
			if (Actual.Num() != Expected.Num())
			{
				Test.AddError(FString::Printf(TEXT("%s: ボクセル数が違う 期待 %d / 実際 %d"), *What, Expected.Num(), Actual.Num()));
				return false;
			}

			bool bOk = true;
			for (int32 Index = 0; Index < Expected.Num(); ++Index)
			{
				bOk &= CheckVec3(Test, FString::Printf(TEXT("%s の %d 番目"), *What, Index), Actual[Index], Expected[Index]);
			}
			return bOk;
		}

		/** ピースが期待どおりか（id とボクセルの並び） */
		bool CheckPiece(FAutomationTestBase& Test, const FString& What, const Cubelith::FPiece& Actual, const Cubelith::FPiece& Expected)
		{
			bool bOk = true;
			if (Actual.Id != Expected.Id)
			{
				Test.AddError(FString::Printf(TEXT("%s: id が違う 期待 %d / 実際 %d"), *What, Expected.Id, Actual.Id));
				bOk = false;
			}
			bOk &= CheckVoxels(Test, What, Actual.Voxels, Expected.Voxels);
			return bOk;
		}

		/** L 字のテトロミノ（TS の L_TETROMINO）。重心に最も近いボクセルが一意に決まる */
		TArray<Cubelith::FVec3> LTetromino()
		{
			return TArray<Cubelith::FVec3>{
				Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(1, 0, 0), Cubelith::Vec3(2, 0, 0), Cubelith::Vec3(2, 1, 0) };
		}

		/** 2 連のピース（TS の placedVoxels の describe が使うもの） */
		TArray<Cubelith::FVec3> Domino()
		{
			return TArray<Cubelith::FVec3>{ Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(1, 0, 0) };
		}

		/** 並びを保ったまま平行移動する */
		TArray<Cubelith::FVec3> Translate(TArrayView<const Cubelith::FVec3> Voxels, const Cubelith::FVec3& Shift)
		{
			TArray<Cubelith::FVec3> Moved;
			Moved.Reserve(Voxels.Num());
			for (const Cubelith::FVec3& V : Voxels)
			{
				Moved.Add(Cubelith::AddVec3(V, Shift));
			}
			return Moved;
		}

		/** 並びを保ったまま向きを適用する */
		TArray<Cubelith::FVec3> Rotate(TArrayView<const Cubelith::FVec3> Voxels, int32 Orientation)
		{
			TArray<Cubelith::FVec3> Rotated;
			Rotated.Reserve(Voxels.Num());
			for (const Cubelith::FVec3& V : Voxels)
			{
				Rotated.Add(Cubelith::RotateVoxel(V, Orientation));
			}
			return Rotated;
		}

		/** 配置を組み立てる */
		Cubelith::FPlacement MakePlacement(int32 PieceId, int32 Orientation, const Cubelith::FVec3& Position)
		{
			Cubelith::FPlacement Placement;
			Placement.PieceId = PieceId;
			Placement.Orientation = Orientation;
			Placement.Position = Position;
			return Placement;
		}

		/** 外接ボックスの Size の 3 成分を昇順に並べたもの（回転では入れ替わるだけのはず） */
		TArray<int32> SortedSize(const Cubelith::FBoundingBox& Box)
		{
			TArray<int32> Values{ Box.Size.X, Box.Size.Y, Box.Size.Z };
			Values.Sort();
			return Values;
		}
	}
}

// ---- LocalOrigin ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceLocalOriginLine3Test, "CUBELITH.Core.Piece.LocalOriginLine3",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceLocalOriginLine3Test::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 重心に最も近いボクセルを返す（直線の 3 連）
	const TArray<Cubelith::FVec3> Voxels{ Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(1, 0, 0), Cubelith::Vec3(2, 0, 0) };
	PieceTestDetail::CheckVec3(*this, TEXT("直線の 3 連の局所原点"), Cubelith::LocalOrigin(Voxels), Cubelith::Vec3(1, 0, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceLocalOriginLShapeTest, "CUBELITH.Core.Piece.LocalOriginLShape",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceLocalOriginLShapeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 重心は (1.25, 0.25, 0)。距離の 2 乗は 1.625 / 0.125 / 0.625 / 1.125 で (1,0,0) が最小
	PieceTestDetail::CheckVec3(*this, TEXT("L 字の 4 連の局所原点"),
		Cubelith::LocalOrigin(PieceTestDetail::LTetromino()), Cubelith::Vec3(1, 0, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceLocalOriginTieLexicographicTest, "CUBELITH.Core.Piece.LocalOriginTieLexicographic",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceLocalOriginTieLexicographicTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 同点のときは辞書順で最小のボクセルを選ぶ
	const TArray<Cubelith::FVec3> PairX{ Cubelith::Vec3(1, 0, 0), Cubelith::Vec3(0, 0, 0) };
	PieceTestDetail::CheckVec3(*this, TEXT("2 連（X 方向）"), Cubelith::LocalOrigin(PairX), Cubelith::Vec3(0, 0, 0));

	const TArray<Cubelith::FVec3> PairZ{ Cubelith::Vec3(0, 0, 6), Cubelith::Vec3(0, 0, 5) };
	PieceTestDetail::CheckVec3(*this, TEXT("2 連（Z 方向）"), Cubelith::LocalOrigin(PairZ), Cubelith::Vec3(0, 0, 5));

	// 2x2 の正方形は 4 つとも重心から等距離
	const TArray<Cubelith::FVec3> Square{
		Cubelith::Vec3(1, 1, 0), Cubelith::Vec3(1, 0, 0), Cubelith::Vec3(0, 1, 0), Cubelith::Vec3(0, 0, 0) };
	PieceTestDetail::CheckVec3(*this, TEXT("2x2 の正方形"), Cubelith::LocalOrigin(Square), Cubelith::Vec3(0, 0, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceLocalOriginTranslationInvariantTest, "CUBELITH.Core.Piece.LocalOriginTranslationInvariant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceLocalOriginTranslationInvariantTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 平行移動しても同じボクセル（を平行移動したもの）を選ぶ
	const Cubelith::FVec3 Shift = Cubelith::Vec3(-3, 7, 2);
	const TArray<Cubelith::FVec3> Base = PieceTestDetail::LTetromino();
	const TArray<Cubelith::FVec3> Moved = PieceTestDetail::Translate(Base, Shift);

	PieceTestDetail::CheckVec3(*this, TEXT("平行移動した L 字の局所原点"),
		Cubelith::LocalOrigin(Moved), Cubelith::AddVec3(Cubelith::LocalOrigin(Base), Shift));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceLocalOriginOrderInvariantTest, "CUBELITH.Core.Piece.LocalOriginOrderInvariant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceLocalOriginOrderInvariantTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 入力の順番を変えても結果が変わらない
	const TArray<Cubelith::FVec3> Base = PieceTestDetail::LTetromino();
	TArray<Cubelith::FVec3> Reversed = Base;
	Algo::Reverse(Reversed);

	PieceTestDetail::CheckVec3(*this, TEXT("逆順にした L 字の局所原点"),
		Cubelith::LocalOrigin(Reversed), Cubelith::LocalOrigin(Base));

	return true;
}

// ---- NormalizePiece / CreatePiece ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceNormalizeMovesOriginToZeroTest, "CUBELITH.Core.Piece.NormalizeMovesOriginToZero",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceNormalizeMovesOriginToZeroTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 局所原点が (0,0,0) に来るよう平行移動する
	Cubelith::FPiece Source;
	Source.Id = 3;
	Source.Voxels = PieceTestDetail::LTetromino();

	const Cubelith::FPiece Piece = Cubelith::NormalizePiece(Source);
	TestEqual(TEXT("id は変わらない"), Piece.Id, 3);

	const TArray<Cubelith::FVec3> Expected{
		Cubelith::Vec3(-1, 0, 0), Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(1, 0, 0), Cubelith::Vec3(1, 1, 0) };
	PieceTestDetail::CheckVoxels(*this, TEXT("正規化後のボクセル"), Piece.Voxels, Expected);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceNormalizeContainsOriginTest, "CUBELITH.Core.Piece.NormalizeContainsOrigin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceNormalizeContainsOriginTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 正規化後は必ず原点のボクセルを含み、局所原点も原点になる
	TArray<TArray<Cubelith::FVec3>> Inputs;
	Inputs.Add(PieceTestDetail::LTetromino());
	Inputs.Add(TArray<Cubelith::FVec3>{ Cubelith::Vec3(5, 5, 5) });
	Inputs.Add(TArray<Cubelith::FVec3>{
		Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(0, 1, 0), Cubelith::Vec3(0, 2, 0), Cubelith::Vec3(0, 2, 1), Cubelith::Vec3(1, 2, 1) });
	Inputs.Add(TArray<Cubelith::FVec3>{ Cubelith::Vec3(-4, -4, -4), Cubelith::Vec3(-3, -4, -4) });

	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const Cubelith::FPiece Piece = Cubelith::CreatePiece(1, Inputs[Index]);
		TestTrue(FString::Printf(TEXT("入力 %d の正規化後が原点を含む"), Index), Piece.Voxels.Contains(Cubelith::Vec3(0, 0, 0)));
		PieceTestDetail::CheckVec3(*this, FString::Printf(TEXT("入力 %d の正規化後の局所原点"), Index),
			Cubelith::LocalOrigin(Piece.Voxels), Cubelith::Vec3(0, 0, 0));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceNormalizeIdempotentTest, "CUBELITH.Core.Piece.NormalizeIdempotent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceNormalizeIdempotentTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 同じ入力に対して常に同じ結果を返し、二度かけても変わらない
	const Cubelith::FPiece Once = Cubelith::CreatePiece(0, PieceTestDetail::LTetromino());
	const Cubelith::FPiece Again = Cubelith::CreatePiece(0, PieceTestDetail::LTetromino());
	PieceTestDetail::CheckPiece(*this, TEXT("2 度作った結果"), Again, Once);
	PieceTestDetail::CheckPiece(*this, TEXT("2 度正規化した結果"), Cubelith::NormalizePiece(Once), Once);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceNormalizeTranslationInvariantTest, "CUBELITH.Core.Piece.NormalizeTranslationInvariant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceNormalizeTranslationInvariantTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 平行移動しただけの入力は同じ正規化結果になる
	const Cubelith::FPiece Base = Cubelith::CreatePiece(2, PieceTestDetail::LTetromino());
	const Cubelith::FPiece Moved = Cubelith::CreatePiece(2,
		PieceTestDetail::Translate(PieceTestDetail::LTetromino(), Cubelith::Vec3(10, -20, 30)));
	PieceTestDetail::CheckPiece(*this, TEXT("平行移動した入力の正規化結果"), Moved, Base);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceNormalizePreservesOrderTest, "CUBELITH.Core.Piece.NormalizePreservesOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceNormalizePreservesOrderTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// ボクセルの並びと個数、相対位置を保つ
	const TArray<Cubelith::FVec3> Source = PieceTestDetail::LTetromino();
	const Cubelith::FPiece Piece = Cubelith::CreatePiece(9, Source);
	TestEqual(TEXT("ボクセル数"), Piece.Voxels.Num(), Source.Num());

	const Cubelith::FVec3 Origin = Cubelith::LocalOrigin(Source);
	for (int32 Index = 0; Index < Source.Num(); ++Index)
	{
		PieceTestDetail::CheckVec3(*this, FString::Printf(TEXT("%d 番目のボクセル"), Index), Piece.Voxels[Index],
			Cubelith::Vec3(Source[Index].X - Origin.X, Source[Index].Y - Origin.Y, Source[Index].Z - Origin.Z));
	}

	return true;
}

// ---- PlacedVoxels ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPiecePlacedVoxelsIdentityAtOriginTest, "CUBELITH.Core.Piece.PlacedVoxelsIdentityAtOrigin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPiecePlacedVoxelsIdentityAtOriginTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 恒等の向きと原点では局所座標のまま（2 連の局所原点は辞書順で (0,0,0) なので並びも変わらない）
	const Cubelith::FPiece Piece = Cubelith::CreatePiece(7, PieceTestDetail::Domino());
	const Cubelith::FPlacement Placement = PieceTestDetail::MakePlacement(7, Cubelith::IdentityOrientation, Cubelith::Vec3(0, 0, 0));

	const TArray<Cubelith::FVec3> Expected{ Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(1, 0, 0) };
	PieceTestDetail::CheckVoxels(*this, TEXT("恒等・原点の配置"), Cubelith::PlacedVoxels(Piece, Placement), Expected);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPiecePlacedVoxelsIdentityWithPositionTest, "CUBELITH.Core.Piece.PlacedVoxelsIdentityWithPosition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPiecePlacedVoxelsIdentityWithPositionTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 恒等の向きでは Position をそのまま加算する
	const Cubelith::FPiece Piece = Cubelith::CreatePiece(7, PieceTestDetail::Domino());
	const Cubelith::FPlacement Placement = PieceTestDetail::MakePlacement(7, Cubelith::IdentityOrientation, Cubelith::Vec3(2, 3, 4));

	const TArray<Cubelith::FVec3> Expected{ Cubelith::Vec3(2, 3, 4), Cubelith::Vec3(3, 3, 4) };
	PieceTestDetail::CheckVoxels(*this, TEXT("恒等・Position (2,3,4)"), Cubelith::PlacedVoxels(Piece, Placement), Expected);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPiecePlacedVoxelsRotateThenTranslateTest, "CUBELITH.Core.Piece.PlacedVoxelsRotateThenTranslate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPiecePlacedVoxelsRotateThenTranslateTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 向きを適用してから Position を加算する（手で並べた期待値）
	// Rz(+90): (x, y, z) -> (-y, x, z) なので (1,0,0) は (0,1,0) に写る
	const Cubelith::FPiece Piece = Cubelith::CreatePiece(7, PieceTestDetail::Domino());
	const Cubelith::FPlacement Placement = PieceTestDetail::MakePlacement(7,
		Cubelith::RotateOrientation(Cubelith::IdentityOrientation, Cubelith::EAxis::Z, 1), Cubelith::Vec3(2, 3, 4));

	const TArray<Cubelith::FVec3> Expected{ Cubelith::Vec3(2, 3, 4), Cubelith::Vec3(2, 4, 4) };
	PieceTestDetail::CheckVoxels(*this, TEXT("Z 軸 +90 度・Position (2,3,4)"), Cubelith::PlacedVoxels(Piece, Placement), Expected);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPiecePlacedVoxelsLShapeYawQuarterTest, "CUBELITH.Core.Piece.PlacedVoxelsLShapeYawQuarter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPiecePlacedVoxelsLShapeYawQuarterTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// Ry(+90): (x, y, z) -> (z, y, -x)。正規化済みの L 字は (-1,0,0) (0,0,0) (1,0,0) (1,1,0)
	const Cubelith::FPiece Piece = Cubelith::CreatePiece(0, PieceTestDetail::LTetromino());
	const Cubelith::FPlacement Placement = PieceTestDetail::MakePlacement(0,
		Cubelith::RotateOrientation(Cubelith::IdentityOrientation, Cubelith::EAxis::Y, 1), Cubelith::Vec3(1, 1, 1));

	const TArray<Cubelith::FVec3> Expected{
		Cubelith::Vec3(1, 1, 2), Cubelith::Vec3(1, 1, 1), Cubelith::Vec3(1, 1, 0), Cubelith::Vec3(1, 2, 0) };
	PieceTestDetail::CheckVoxels(*this, TEXT("L 字を Y 軸に 90 度"), Cubelith::PlacedVoxels(Piece, Placement), Expected);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPiecePlacedVoxelsPositionShiftsTest, "CUBELITH.Core.Piece.PlacedVoxelsPositionShifts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPiecePlacedVoxelsPositionShiftsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// Position をずらすと結果も同じだけ平行移動する
	const Cubelith::FPiece Piece = Cubelith::CreatePiece(7, PieceTestDetail::Domino());
	const Cubelith::FVec3 Delta = Cubelith::Vec3(-5, 2, 8);

	for (int32 Orientation = 0; Orientation < Cubelith::OrientationCount; ++Orientation)
	{
		const TArray<Cubelith::FVec3> Base = Cubelith::PlacedVoxels(Piece,
			PieceTestDetail::MakePlacement(7, Orientation, Cubelith::Vec3(0, 0, 0)));
		const TArray<Cubelith::FVec3> Moved = Cubelith::PlacedVoxels(Piece,
			PieceTestDetail::MakePlacement(7, Orientation, Delta));

		PieceTestDetail::CheckVoxels(*this, FString::Printf(TEXT("向き %d で Position をずらす"), Orientation),
			Moved, PieceTestDetail::Translate(Base, Delta));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPiecePlacedVoxelsAllOrientationsTest, "CUBELITH.Core.Piece.PlacedVoxelsAllOrientations",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPiecePlacedVoxelsAllOrientationsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// どの向き（24 通り）でもボクセル数は変わらず、RotateVoxel + 加算と一致する
	const Cubelith::FPiece Piece = Cubelith::CreatePiece(4, PieceTestDetail::LTetromino());
	const Cubelith::FVec3 Position = Cubelith::Vec3(3, -1, 2);

	for (int32 Orientation = 0; Orientation < Cubelith::OrientationCount; ++Orientation)
	{
		const TArray<Cubelith::FVec3> Placed = Cubelith::PlacedVoxels(Piece,
			PieceTestDetail::MakePlacement(4, Orientation, Position));
		TestEqual(FString::Printf(TEXT("向き %d のボクセル数"), Orientation), Placed.Num(), Piece.Voxels.Num());

		const TSet<Cubelith::FVec3> Unique(Placed);
		TestEqual(FString::Printf(TEXT("向き %d で重なりが無い"), Orientation), Unique.Num(), Piece.Voxels.Num());

		const TArray<Cubelith::FVec3> Expected =
			PieceTestDetail::Translate(PieceTestDetail::Rotate(Piece.Voxels, Orientation), Position);
		PieceTestDetail::CheckVoxels(*this, FString::Printf(TEXT("向き %d の配置"), Orientation), Placed, Expected);
	}

	return true;
}

// ---- BoundingBox ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceBoundingBoxMinMaxSizeTest, "CUBELITH.Core.Piece.BoundingBoxMinMaxSize",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceBoundingBoxMinMaxSizeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// Min / Max / Size を返す
	const TArray<Cubelith::FVec3> Voxels{
		Cubelith::Vec3(-1, 0, 0), Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(1, 0, 0), Cubelith::Vec3(1, 1, 0) };
	const Cubelith::FBoundingBox Box = Cubelith::BoundingBox(Voxels);

	PieceTestDetail::CheckVec3(*this, TEXT("Min"), Box.Min, Cubelith::Vec3(-1, 0, 0));
	PieceTestDetail::CheckVec3(*this, TEXT("Max"), Box.Max, Cubelith::Vec3(1, 1, 0));
	PieceTestDetail::CheckVec3(*this, TEXT("Size"), Box.Size, Cubelith::Vec3(3, 2, 1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceBoundingBoxSingleVoxelTest, "CUBELITH.Core.Piece.BoundingBoxSingleVoxel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceBoundingBoxSingleVoxelTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// ボクセル 1 つなら Size は (1,1,1)
	const TArray<Cubelith::FVec3> Voxels{ Cubelith::Vec3(4, -2, 7) };
	const Cubelith::FBoundingBox Box = Cubelith::BoundingBox(Voxels);

	PieceTestDetail::CheckVec3(*this, TEXT("Min"), Box.Min, Cubelith::Vec3(4, -2, 7));
	PieceTestDetail::CheckVec3(*this, TEXT("Max"), Box.Max, Cubelith::Vec3(4, -2, 7));
	PieceTestDetail::CheckVec3(*this, TEXT("Size"), Box.Size, Cubelith::Vec3(1, 1, 1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceBoundingBoxSizePermutedByRotationTest, "CUBELITH.Core.Piece.BoundingBoxSizePermutedByRotation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceBoundingBoxSizePermutedByRotationTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 回転しても Size の 3 成分は入れ替わるだけ
	const TArray<Cubelith::FVec3> Base = PieceTestDetail::LTetromino();
	const TArray<int32> BaseSorted = PieceTestDetail::SortedSize(Cubelith::BoundingBox(Base));

	for (int32 Orientation = 0; Orientation < Cubelith::OrientationCount; ++Orientation)
	{
		const TArray<int32> Sorted = PieceTestDetail::SortedSize(
			Cubelith::BoundingBox(PieceTestDetail::Rotate(Base, Orientation)));
		for (int32 Index = 0; Index < BaseSorted.Num(); ++Index)
		{
			if (Sorted[Index] != BaseSorted[Index])
			{
				AddError(FString::Printf(TEXT("向き %d で Size の %d 番目（昇順）が変わった: 期待 %d / 実際 %d"),
					Orientation, Index, BaseSorted[Index], Sorted[Index]));
			}
		}
	}

	return true;
}

// ---- 照合データから座標と配置を読む（ここでは形だけ。生成結果との一致は 005 以降で確かめる） ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPieceFixtureShapeTest, "CUBELITH.Core.Piece.FixtureShape",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPieceFixtureShapeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	FString Error;
	const TSharedPtr<FJsonObject> Puzzle = LoadFixtureJson(TEXT("puzzle_n3_m3.json"), Error);
	if (!Puzzle.IsValid())
	{
		AddError(Error);
		return false;
	}

	int32 N = 0;
	int32 M = 0;
	if (!Puzzle->TryGetNumberField(TEXT("n"), N) || !Puzzle->TryGetNumberField(TEXT("m"), M))
	{
		AddError(TEXT("puzzle_n3_m3.json に n / m が無い"));
		return false;
	}
	TestEqual(TEXT("n"), N, 3);
	TestEqual(TEXT("m"), M, 3);

	const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
	if (!Puzzle->TryGetArrayField(TEXT("cases"), Cases) || Cases == nullptr || Cases->Num() == 0)
	{
		AddError(TEXT("puzzle_n3_m3.json の cases が読めない"));
		return false;
	}

	const TSharedPtr<FJsonObject>* Case = nullptr;
	if (!(*Cases)[0].IsValid() || !(*Cases)[0]->TryGetObject(Case) || Case == nullptr || !Case->IsValid())
	{
		AddError(TEXT("cases[0] がオブジェクトとして読めない"));
		return false;
	}

	// pieces[] の形。ピース id は 0..M-1 の昇順（Docs/FIXTURES.md「共通の表し方」）
	const TArray<TSharedPtr<FJsonValue>>* Pieces = nullptr;
	if (!(*Case)->TryGetArrayField(TEXT("pieces"), Pieces) || Pieces == nullptr)
	{
		AddError(TEXT("cases[0].pieces が読めない"));
		return false;
	}
	TestEqual(TEXT("pieces の件数"), Pieces->Num(), M);

	int32 TotalVoxels = 0;
	for (int32 Index = 0; Index < Pieces->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* PieceObject = nullptr;
		if (!(*Pieces)[Index].IsValid() || !(*Pieces)[Index]->TryGetObject(PieceObject) || PieceObject == nullptr || !PieceObject->IsValid())
		{
			AddError(FString::Printf(TEXT("pieces[%d] がオブジェクトとして読めない"), Index));
			return false;
		}

		int32 PieceId = -1;
		if (!(*PieceObject)->TryGetNumberField(TEXT("id"), PieceId))
		{
			AddError(FString::Printf(TEXT("pieces[%d] に id が無い"), Index));
			return false;
		}
		TestEqual(FString::Printf(TEXT("pieces[%d].id"), Index), PieceId, Index);

		TArray<Cubelith::FVec3> Voxels;
		if (!ReadVec3Array(*PieceObject, TEXT("voxels"), Voxels, Error))
		{
			AddError(Error);
			return false;
		}
		TestTrue(FString::Printf(TEXT("pieces[%d].voxels が空でない"), Index), Voxels.Num() > 0);

		// 照合データのピースは正規化済み。局所原点が原点に来ていることで読み出しの形を確かめる
		if (Voxels.Num() > 0)
		{
			TestTrue(FString::Printf(TEXT("pieces[%d].voxels が原点を含む"), Index), Voxels.Contains(Cubelith::Vec3(0, 0, 0)));
			PieceTestDetail::CheckVec3(*this, FString::Printf(TEXT("pieces[%d] の局所原点"), Index),
				Cubelith::LocalOrigin(Voxels), Cubelith::Vec3(0, 0, 0));
		}

		TotalVoxels += Voxels.Num();
	}
	// 全ピースで N^3 マスを分け合う（どのボクセルがどのピースかの一致は 005 以降）
	TestEqual(TEXT("ボクセル数の合計"), TotalVoxels, N * N * N);

	// solution[] の形。cases[0] は回転なしなので向きはすべて恒等
	TArray<Cubelith::FPlacement> Solution;
	if (!ReadPlacementArray(*Case, TEXT("solution"), Solution, Error))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("solution の件数"), Solution.Num(), M);

	bool bAllowRotation = true;
	if (!(*Case)->TryGetBoolField(TEXT("allowRotation"), bAllowRotation))
	{
		AddError(TEXT("cases[0] に allowRotation が無い"));
		return false;
	}
	TestFalse(TEXT("cases[0] は回転なし"), bAllowRotation);

	for (int32 Index = 0; Index < Solution.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("solution[%d].pieceId"), Index), Solution[Index].PieceId, Index);
		if (!bAllowRotation)
		{
			TestEqual(FString::Printf(TEXT("solution[%d].orientation"), Index), Solution[Index].Orientation, Cubelith::IdentityOrientation);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
