// Cubelith::FSnapControl（手を離したときのマグネット・スナップの制御。RULES.md 3.5 / 5.1）のテスト。
// 移植元 WebMock/src/input/snapControl.ts にテストは無いので、TS の振る舞いのうち
// 「ここで決めていること」を確かめる: 1 マス隣にずらした配置が吸着先になる / すでに収まっている配置は
// 吸着先にならない / 対象なし（INDEX_NONE）では候補が出ない / 光らせる対象が変わったときだけ変化として報告される。
// 候補そのものの正しさ（RULES.md 3.5 の 3 条件）は CUBELITH.Core.Snap.* が見ているので、ここでは見ない。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithSnapControl.h"
#include "Grid.h"
#include "Piece.h"

namespace CubelithRenderTests
{
	// SnapControlTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、
	// 名前をこの中に閉じる
	namespace SnapControlTestDetail
	{
		/** 配置を組み立てる小さな入口（SnapTest.cpp の Place と同じ）。向きは既定で恒等 */
		Cubelith::FPlacement Place(int32 PieceId, const Cubelith::FVec3& Position,
			int32 Orientation = Cubelith::IdentityOrientation)
		{
			Cubelith::FPlacement Placement;
			Placement.PieceId = PieceId;
			Placement.Orientation = Orientation;
			Placement.Position = Position;
			return Placement;
		}

		/**
		 * x 方向に 2 マス続くドミノ 2 個（CUBELITH.Core.Snap.OneStepOffReturnsSolution と同じ盤面）。
		 * N=3 で、ピース 0 が (0,0,0)、ピース 1 の収まる位置が (0,1,0)
		 */
		TArray<Cubelith::FPiece> Dominoes()
		{
			const TArray<Cubelith::FVec3> Voxels{ Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(1, 0, 0) };
			return TArray<Cubelith::FPiece>{ Cubelith::CreatePiece(0, Voxels), Cubelith::CreatePiece(1, Voxels) };
		}

		/** 上のドミノ 2 個の解答空間のサイズ */
		constexpr int32 DominoN = 3;

		/** 期待する座標と一致するか（SnapTest.cpp の CheckVec3 と同じ） */
		bool CheckVec3(FAutomationTestBase& Test, const FString& What,
			const Cubelith::FVec3& Actual, const Cubelith::FVec3& Expected)
		{
			if (Actual.X == Expected.X && Actual.Y == Expected.Y && Actual.Z == Expected.Z)
			{
				return true;
			}
			Test.AddError(FString::Printf(TEXT("%s: 期待 (%d,%d,%d) / 実際 (%d,%d,%d)"),
				*What, Expected.X, Expected.Y, Expected.Z, Actual.X, Actual.Y, Actual.Z));
			return false;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapOneStepOffSnapsTest, "CUBELITH.Render.Snap.OneStepOffSnaps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapOneStepOffSnapsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SnapControlTestDetail;

	// ピース 1 は収まる位置 (0,1,0) の 1 マス上にずれている
	const TArray<Cubelith::FPiece> Pieces = Dominoes();
	const TArray<Cubelith::FPlacement> Placements{ Place(0, Cubelith::Vec3(0, 0, 0)), Place(1, Cubelith::Vec3(0, 2, 0)) };

	Cubelith::FSnapControl Control(Pieces, DominoN);

	// 候補があるので発光の対象になり、それは「変わった」として報告される
	TestTrue(TEXT("候補が出たので変化として報告される"), Control.Refresh(Placements, 1));
	TestEqual(TEXT("光らせる対象"), Control.HintedPieceId(), 1);

	const TOptional<Cubelith::FSnapTarget> Target = Control.Release(Placements, 1);
	if (!Target.IsSet())
	{
		AddError(TEXT("吸着先が返らなかった"));
		return false;
	}

	TestEqual(TEXT("吸着前のピース id"), Target->From.PieceId, 1);
	CheckVec3(*this, TEXT("吸着前の位置"), Target->From.Position, Cubelith::Vec3(0, 2, 0));
	TestEqual(TEXT("吸着後のピース id"), Target->To.PieceId, 1);
	CheckVec3(*this, TEXT("吸着後の位置"), Target->To.Position, Cubelith::Vec3(0, 1, 0));
	// スナップは平行移動だけ（RULES.md 3.5。向きは変わらない）
	TestEqual(TEXT("吸着後の向き"), Target->To.Orientation, Cubelith::IdentityOrientation);

	// 手を離したら発光は消える（吸着したかどうかによらない）
	TestEqual(TEXT("離した後の光らせる対象"), Control.HintedPieceId(), INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapSettledPieceDoesNotSnapTest, "CUBELITH.Render.Snap.SettledPieceDoesNotSnap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapSettledPieceDoesNotSnapTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SnapControlTestDetail;

	// ピース 1 は既に収まっている。Cubelith::SnapCandidate は移動量 0 の候補を返す仕様（Solve.h）だが、
	// それは「吸い付く先」ではないので FSnapControl が捨てる（TS の compute と同じ）
	const TArray<Cubelith::FPiece> Pieces = Dominoes();
	const TArray<Cubelith::FPlacement> Placements{ Place(0, Cubelith::Vec3(0, 0, 0)), Place(1, Cubelith::Vec3(0, 1, 0)) };

	Cubelith::FSnapControl Control(Pieces, DominoN);

	TestFalse(TEXT("すでに収まっているので発光の対象は変わらない"), Control.Refresh(Placements, 1));
	TestEqual(TEXT("光らせる対象"), Control.HintedPieceId(), INDEX_NONE);
	TestFalse(TEXT("吸着先は返らない"), Control.Release(Placements, 1).IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapNoActivePieceHasNoHintTest, "CUBELITH.Render.Snap.NoActivePieceHasNoHint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapNoActivePieceHasNoHintTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SnapControlTestDetail;

	// 対象なし（INDEX_NONE。TS の pieceId === null）では候補を計算しない。
	// 固定中のピースを弾くのは呼び出し側で、その結果がこの呼び方になる
	const TArray<Cubelith::FPiece> Pieces = Dominoes();
	const TArray<Cubelith::FPlacement> Placements{ Place(0, Cubelith::Vec3(0, 0, 0)), Place(1, Cubelith::Vec3(0, 2, 0)) };

	Cubelith::FSnapControl Control(Pieces, DominoN);

	TestFalse(TEXT("対象なしでは発光の対象は変わらない"), Control.Refresh(Placements, INDEX_NONE));
	TestEqual(TEXT("光らせる対象"), Control.HintedPieceId(), INDEX_NONE);

	// 候補が出たあとに対象なしへ切り替えると、発光が消えるので「変わった」として報告される
	TestTrue(TEXT("候補が出た"), Control.Refresh(Placements, 1));
	TestTrue(TEXT("対象なしへ戻したら変化として報告される"), Control.Refresh(Placements, INDEX_NONE));
	TestEqual(TEXT("光らせる対象"), Control.HintedPieceId(), INDEX_NONE);

	// 配置に無いピース id も「候補なし」として扱う（SnapCandidate の checkf まで行かせない）
	TestFalse(TEXT("配置に無い id では発光の対象は変わらない"), Control.Refresh(Placements, 99));
	TestFalse(TEXT("配置に無い id では吸着先も返らない"), Control.Release(Placements, 99).IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapHintChangeReportedOnceTest, "CUBELITH.Render.Snap.HintChangeReportedOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapHintChangeReportedOnceTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SnapControlTestDetail;

	// 同じ対象を続けて Refresh しても 2 回目は変化なし（TS が onHintChange を出さないのと同じ）。
	// 見た目の書き直しをこの判定で 1 回に抑えている
	const TArray<Cubelith::FPiece> Pieces = Dominoes();
	const TArray<Cubelith::FPlacement> Placements{ Place(0, Cubelith::Vec3(0, 0, 0)), Place(1, Cubelith::Vec3(0, 2, 0)) };

	Cubelith::FSnapControl Control(Pieces, DominoN);

	TestTrue(TEXT("1 回目は変化として報告される"), Control.Refresh(Placements, 1));
	TestFalse(TEXT("2 回目は変化なし"), Control.Refresh(Placements, 1));
	TestFalse(TEXT("3 回目も変化なし"), Control.Refresh(Placements, 1));
	TestEqual(TEXT("光らせる対象は変わらない"), Control.HintedPieceId(), 1);

	// 発光していたピースが収まったら、そこで 1 回だけ「消えた」と報告される
	const TArray<Cubelith::FPlacement> Settled{ Place(0, Cubelith::Vec3(0, 0, 0)), Place(1, Cubelith::Vec3(0, 1, 0)) };
	TestTrue(TEXT("収まったので消えたと報告される"), Control.Refresh(Settled, 1));
	TestEqual(TEXT("光らせる対象"), Control.HintedPieceId(), INDEX_NONE);
	TestFalse(TEXT("続けて呼んでも変化なし"), Control.Refresh(Settled, 1));

	// 別のピースへ対象が移ったときも 1 回だけ報告される（ピース 0 側が 1 マスずれている盤面）
	const TArray<Cubelith::FPlacement> OtherOff{ Place(0, Cubelith::Vec3(0, 2, 0)), Place(1, Cubelith::Vec3(0, 0, 0)) };
	TestTrue(TEXT("ピース 0 が候補になった"), Control.Refresh(OtherOff, 0));
	TestEqual(TEXT("光らせる対象"), Control.HintedPieceId(), 0);
	TestFalse(TEXT("続けて呼んでも変化なし"), Control.Refresh(OtherOff, 0));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
