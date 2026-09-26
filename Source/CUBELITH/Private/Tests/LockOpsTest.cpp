// CubelithLockOps（固定 / 固定解除・ヒント・散らし直しを繋ぐときの純粋な判断）のテスト。
// 固定・ヒント・散らし直しの本体は CUBELITHCore 側でテスト済み（CUBELITH.Core.Game.* /
// CUBELITH.Core.Hint.* / CUBELITH.Core.Generate.*）なので、ここで見るのはこのタスクで書いた判断だけ。
// 見るのは 4 つ: 固定の切り替えの分岐（ヒントは解除できない）/ ヒントが使える条件 /
// 散らし直しで残す配置の集め方 / 鍵アイコンの色の選び方。
// アクタもウィジェットも立てない（Scripts/Test.ps1 は -nullrhi で回る）。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithLockOps.h"
#include "Game.h"
#include "Grid.h"
#include "Piece.h"

namespace CubelithRenderTests
{
	// LockOpsTest.cpp 専用のヘルパ。unity ビルドでは他のテストと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace LockOpsTestDetail
	{
		/** 配置を組み立てる小さな入口（ProgressTest.cpp の Place と同じ） */
		Cubelith::FPlacement Place(int32 PieceId, const Cubelith::FVec3& Position)
		{
			Cubelith::FPlacement Placement;
			Placement.PieceId = PieceId;
			Placement.Orientation = Cubelith::IdentityOrientation;
			Placement.Position = Position;
			return Placement;
		}

		/** ピース id → 固定の種類の表から Cubelith::FGame::LockKindOf と同じ形の引きを作る */
		TOptional<Cubelith::ELockKind> LookUp(const TMap<int32, Cubelith::ELockKind>& Locks, int32 PieceId)
		{
			const Cubelith::ELockKind* Found = Locks.Find(PieceId);
			return (Found != nullptr) ? TOptional<Cubelith::ELockKind>(*Found) : TOptional<Cubelith::ELockKind>();
		}
	}
}

// 1. 固定の切り替えの分岐: 未固定 → 固定、手動の固定 → 解除、ヒントの固定 → 何もしない（RULES.md 3.3）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithLockOpsDecideToggleTest, "CUBELITH.Render.LockOps.DecideToggle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithLockOpsDecideToggleTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("未固定なら手動の固定を付ける"),
		static_cast<int32>(Cubelith::DecideLockToggle(TOptional<Cubelith::ELockKind>())),
		static_cast<int32>(Cubelith::ELockToggleAction::Lock));

	TestEqual(TEXT("手動の固定なら外す"),
		static_cast<int32>(Cubelith::DecideLockToggle(TOptional<Cubelith::ELockKind>(Cubelith::ELockKind::Manual))),
		static_cast<int32>(Cubelith::ELockToggleAction::Unlock));

	TestEqual(TEXT("ヒントの固定は解除できない"),
		static_cast<int32>(Cubelith::DecideLockToggle(TOptional<Cubelith::ELockKind>(Cubelith::ELockKind::Hint))),
		static_cast<int32>(Cubelith::ELockToggleAction::Blocked));

	return true;
}

// 2. ヒントが使えるのは未固定が 2 個以上のときだけ（RULES.md 3.7。最後の 1 ピースは埋めさせない）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithLockOpsHintAvailableTest, "CUBELITH.Render.LockOps.HintAvailable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithLockOpsHintAvailableTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		int32 PieceCount;
		int32 LockedCount;
		bool bExpected;
	};

	const FCase Cases[7] = {
		{ 4, 0, true },   // 何も固定していない
		{ 4, 1, true },   // 未固定 3 個
		{ 4, 2, true },   // 未固定 2 個（境目。使える側）
		{ 4, 3, false },  // 未固定 1 個（境目。使えない側）
		{ 4, 4, false },  // 全部固定
		{ 2, 0, true },   // ピースが最少（M=2）でも未固定 2 個なら使える
		{ 2, 1, false },  // 未固定 1 個
	};

	for (const FCase& Case : Cases)
	{
		const bool bActual = Cubelith::IsHintAvailable(Case.PieceCount, Case.LockedCount);
		if (bActual != Case.bExpected)
		{
			AddError(FString::Printf(TEXT("ピース %d 個・固定 %d 個でヒントが %s（期待 %s）"),
				Case.PieceCount, Case.LockedCount,
				bActual ? TEXT("使える") : TEXT("使えない"),
				Case.bExpected ? TEXT("使える") : TEXT("使えない")));
		}
	}

	return true;
}

// 3. 散らし直しで残すのはヒントの固定が付いたピースの現在の配置だけ（RULES.md 3.3「やり直し」）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithLockOpsHintKeptPlacementsTest, "CUBELITH.Render.LockOps.HintKeptPlacements",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithLockOpsHintKeptPlacementsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::LockOpsTestDetail;

	const TArray<Cubelith::FPlacement> Placements = {
		Place(0, Cubelith::Vec3(1, 2, 3)),
		Place(1, Cubelith::Vec3(4, 5, 6)),
		Place(2, Cubelith::Vec3(7, 8, 9)),
		Place(3, Cubelith::Vec3(-1, -2, -3)),
	};

	TMap<int32, Cubelith::ELockKind> Locks;
	Locks.Add(0, Cubelith::ELockKind::Manual);
	Locks.Add(2, Cubelith::ELockKind::Hint);
	Locks.Add(3, Cubelith::ELockKind::Hint);
	// ピース 1 は未固定

	const TArray<Cubelith::FPlacement> Kept = Cubelith::CollectHintKeptPlacements(Placements,
		[&Locks](int32 PieceId) { return LookUp(Locks, PieceId); });

	if (Kept.Num() != 2)
	{
		AddError(FString::Printf(TEXT("残す配置が %d 個（期待 2 個。ヒントの固定だけ）"), Kept.Num()));
		return true;
	}

	// 並びは渡した配置の並びを保つ（id 2 → id 3）
	TestEqual(TEXT("1 個目はピース 2"), Kept[0].PieceId, 2);
	TestEqual(TEXT("2 個目はピース 3"), Kept[1].PieceId, 3);

	// 「現在の」配置をそのまま持つ（位置と向きを写し替えない）
	TestTrue(TEXT("ピース 2 の位置がそのまま"), Cubelith::EqualsVec3(Kept[0].Position, Cubelith::Vec3(7, 8, 9)));
	TestTrue(TEXT("ピース 3 の位置がそのまま"), Cubelith::EqualsVec3(Kept[1].Position, Cubelith::Vec3(-1, -2, -3)));

	// 固定が 1 つも無ければ空（= 最初の散らしと同じ配置に戻る）
	const TArray<Cubelith::FPlacement> NoneKept = Cubelith::CollectHintKeptPlacements(Placements,
		[](int32) { return TOptional<Cubelith::ELockKind>(); });
	TestEqual(TEXT("固定が無ければ残す配置も無い"), NoneKept.Num(), 0);

	// 手動の固定だけなら空（手動の固定は残さない。FGame::Reset が手動の固定を解くのと揃う）
	TMap<int32, Cubelith::ELockKind> ManualOnly;
	ManualOnly.Add(0, Cubelith::ELockKind::Manual);
	ManualOnly.Add(1, Cubelith::ELockKind::Manual);
	const TArray<Cubelith::FPlacement> ManualKept = Cubelith::CollectHintKeptPlacements(Placements,
		[&ManualOnly](int32 PieceId) { return LookUp(ManualOnly, PieceId); });
	TestEqual(TEXT("手動の固定は残さない"), ManualKept.Num(), 0);

	return true;
}

// 4. 鍵アイコンの色は 銀 = 手動の固定 / 金 = ヒント（RULES.md 6 章）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithLockOpsIconColorTest, "CUBELITH.Render.LockOps.IconColor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithLockOpsIconColorTest::RunTest(const FString& Parameters)
{
	// 人がエディタで入れる色を模した、見分けの付く 2 色（色そのものは UPROPERTY なのでここでは値を問わない）
	const FLinearColor Silver(0.85f, 0.88f, 0.94f);
	const FLinearColor Gold(1.0f, 0.78f, 0.12f);

	TestTrue(TEXT("手動の固定は銀"),
		Cubelith::LockIconColor(Cubelith::ELockKind::Manual, Silver, Gold).Equals(Silver));
	TestTrue(TEXT("ヒントの固定は金"),
		Cubelith::LockIconColor(Cubelith::ELockKind::Hint, Silver, Gold).Equals(Gold));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
