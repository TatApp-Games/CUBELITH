// Cubelith::FSnapMotion（スナップの補間移動。RULES.md 3.5）のテスト。
// 移植元 WebMock/src/render/snapMotion.ts にテストは無いので、TS の振る舞いをそのまま確かめる:
// 開始直後はずれが満額 / 所要時間を過ぎたらずれが消える / 途中の値が単調に減る /
// 同じピースに 2 回 Start したら前の補間が捨てられる / ずれ 0 で Start したら即座にずれ無しになる。
// 時間は呼び出し側から渡す形（FSnapMotion.h の「解釈:」）なので、実時間を待たずに進められる。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/ArrayView.h"

#include "CubelithSnapMotion.h"
#include "Grid.h"

namespace CubelithRenderTests
{
	// SnapMotionTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、
	// 名前をこの中に閉じる
	namespace SnapMotionTestDetail
	{
		/** 確かめに使う補間時間（秒）。既定と同じ 130 ms */
		constexpr double Duration = Cubelith::SnapDurationSeconds;

		/** 比較の許容差（easing の計算は double なので緩める必要は無いが、桁落ちの余地を残す） */
		constexpr double Tolerance = 1.0e-9;

		/**
		 * 列挙されたずれのうち、そのピースのものを探す。無ければ nullptr
		 * （「このピースについて何も起きなかった」の確かめにも使う）
		 */
		const Cubelith::FSnapOffsetUpdate* Find(TArrayView<const Cubelith::FSnapOffsetUpdate> Updates, int32 PieceId)
		{
			for (const Cubelith::FSnapOffsetUpdate& Update : Updates)
			{
				if (Update.PieceId == PieceId)
				{
					return &Update;
				}
			}
			return nullptr;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapMotionStartsAtFullOffsetTest, "CUBELITH.Render.SnapMotion.StartsAtFullOffset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapMotionStartsAtFullOffsetTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SnapMotionTestDetail;

	// 開始直後は「移動前 − 移動後」がそのまま乗る ＝ 見た目が移動前の位置から始まる
	Cubelith::FSnapMotion Motion(Duration);
	TArray<Cubelith::FSnapOffsetUpdate> Updates;

	Motion.Start(3, Cubelith::Vec3(0, 1, 0), 10.0, Updates);

	if (Updates.Num() != 1)
	{
		AddError(FString::Printf(TEXT("Start が出したずれの件数: 期待 1 / 実際 %d"), Updates.Num()));
		return false;
	}
	TestEqual(TEXT("ピース id"), Updates[0].PieceId, 3);
	TestFalse(TEXT("ずれ無しではない"), Updates[0].bCleared);
	TestEqual(TEXT("ずれ（グリッド単位）"), Updates[0].Offset, FVector(0.0, 1.0, 0.0));
	TestTrue(TEXT("補間が走っている"), Motion.IsRunning(3));
	TestEqual(TEXT("走っている本数"), Motion.Num(), 1);

	// 開始と同じ時刻で Update しても満額のまま（t = 0 → easing も 0）
	Motion.Update(10.0, Updates);
	const Cubelith::FSnapOffsetUpdate* AtStart = Find(Updates, 3);
	if (AtStart == nullptr)
	{
		AddError(TEXT("開始と同じ時刻の Update でずれが出なかった"));
		return false;
	}
	TestFalse(TEXT("まだずれ無しにはならない"), AtStart->bCleared);
	TestTrue(TEXT("開始時刻ではずれが満額"), FMath::IsNearlyEqual(AtStart->Offset.Y, 1.0, Tolerance));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapMotionClearsAfterDurationTest, "CUBELITH.Render.SnapMotion.ClearsAfterDuration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapMotionClearsAfterDurationTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SnapMotionTestDetail;

	// 所要時間を過ぎたらずれが消える（= 論理位置そのままに戻る）
	Cubelith::FSnapMotion Motion(Duration);
	TArray<Cubelith::FSnapOffsetUpdate> Updates;

	// 開始を 0 秒にしてあるのは、経過 / 所要時間がちょうど 1.0 になる（x / x は誤差なく 1）ようにして
	// 「所要時間ちょうど」の境目を確かめるため。実際の呼び出しでは UWorld の時刻が来るので端数が付く
	Motion.Start(0, Cubelith::Vec3(-1, 0, 2), 0.0, Updates);
	Motion.Update(Duration, Updates);

	if (Updates.Num() != 1)
	{
		AddError(FString::Printf(TEXT("所要時間ちょうどで出たずれの件数: 期待 1 / 実際 %d"), Updates.Num()));
		return false;
	}
	TestEqual(TEXT("ピース id"), Updates[0].PieceId, 0);
	TestTrue(TEXT("ずれ無しに戻る"), Updates[0].bCleared);
	TestFalse(TEXT("補間はもう走っていない"), Motion.IsRunning(0));
	TestEqual(TEXT("走っている本数"), Motion.Num(), 0);

	// 終わったあとの Update は何も出さない（毎フレーム呼ばれるので空で返ることが要る）
	Motion.Update(Duration * 2.0, Updates);
	TestEqual(TEXT("終わったあとは何も出ない"), Updates.Num(), 0);

	// 所要時間を越えた時刻で初めて Update が来ても（フレームが飛んだとき）、そこで畳まれる
	Motion.Start(0, Cubelith::Vec3(-1, 0, 2), 5.0, Updates);
	Motion.Update(5.0 + Duration * 3.0, Updates);
	if (Updates.Num() != 1)
	{
		AddError(FString::Printf(TEXT("所要時間を越えた Update で出た件数: 期待 1 / 実際 %d"), Updates.Num()));
		return false;
	}
	TestTrue(TEXT("越えていればずれ無しに戻る"), Updates[0].bCleared);
	TestEqual(TEXT("走っている本数"), Motion.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapMotionDecreasesMonotonicallyTest, "CUBELITH.Render.SnapMotion.DecreasesMonotonically",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapMotionDecreasesMonotonicallyTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SnapMotionTestDetail;

	// 途中の値は単調に減る（easeOutCubic が単調増加なので、残り = 1 - ease は単調減少）。
	// 2 マス分のずれで確かめて、各軸が同じ比で縮むことも見る
	Cubelith::FSnapMotion Motion(Duration);
	TArray<Cubelith::FSnapOffsetUpdate> Updates;

	const double StartSeconds = 100.0;
	Motion.Start(7, Cubelith::Vec3(2, 0, -2), StartSeconds, Updates);

	double Previous = 2.0;
	for (int32 Step = 1; Step <= 12; ++Step)
	{
		const double NowSeconds = StartSeconds + Duration * static_cast<double>(Step) / 13.0;
		Motion.Update(NowSeconds, Updates);

		const Cubelith::FSnapOffsetUpdate* Update = Find(Updates, 7);
		if (Update == nullptr || Update->bCleared)
		{
			AddError(FString::Printf(TEXT("%d 歩目で補間が終わってしまった（まだ所要時間内）"), Step));
			return false;
		}

		const double Current = Update->Offset.X;
		if (!(Current < Previous))
		{
			AddError(FString::Printf(TEXT("%d 歩目でずれが減っていない: 前 %f / 今 %f"), Step, Previous, Current));
			return false;
		}
		// z は -2 から 0 へ向かうので、x と符号が逆で大きさは同じ
		TestTrue(TEXT("軸ごとに同じ比で縮む"), FMath::IsNearlyEqual(Update->Offset.Z, -Current, Tolerance));
		TestTrue(TEXT("動いていない軸は 0 のまま"), FMath::IsNearlyEqual(Update->Offset.Y, 0.0, Tolerance));

		Previous = Current;
	}

	TestTrue(TEXT("最後まで満額より小さい"), Previous < 2.0);
	TestTrue(TEXT("最後は 0 に近い"), Previous < 0.1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapMotionRestartReplacesPreviousTest, "CUBELITH.Render.SnapMotion.RestartReplacesPrevious",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapMotionRestartReplacesPreviousTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SnapMotionTestDetail;

	// 同じピースに 2 回 Start したら前の補間は捨てられる（2 本走らせない）。
	// 2 本走ると表示のずれが上書きし合って「戻って進む」が見えるので、ここが崩れると手触りが壊れる
	Cubelith::FSnapMotion Motion(Duration);
	TArray<Cubelith::FSnapOffsetUpdate> Updates;

	Motion.Start(1, Cubelith::Vec3(1, 0, 0), 0.0, Updates);
	// 1 本目が半分ほど進んだところで 2 本目を始める
	Motion.Update(Duration * 0.5, Updates);
	Motion.Start(1, Cubelith::Vec3(0, 0, -1), Duration * 0.5, Updates);

	TestEqual(TEXT("走っている本数は 1 本"), Motion.Num(), 1);
	if (Updates.Num() != 1)
	{
		AddError(FString::Printf(TEXT("2 回目の Start が出したずれの件数: 期待 1 / 実際 %d"), Updates.Num()));
		return false;
	}
	// 新しいずれが満額で乗る（前のずれは混ざらない）
	TestEqual(TEXT("新しいずれ"), Updates[0].Offset, FVector(0.0, 0.0, -1.0));

	// 2 本目の開始時刻から数えて所要時間で終わる（1 本目の開始時刻は忘れている）
	Motion.Update(Duration * 1.4, Updates);
	const Cubelith::FSnapOffsetUpdate* Middle = Find(Updates, 1);
	if (Middle == nullptr || Middle->bCleared)
	{
		AddError(TEXT("2 本目が早く終わってしまった（1 本目の開始時刻で数えている）"));
		return false;
	}

	Motion.Update(Duration * 1.5, Updates);
	const Cubelith::FSnapOffsetUpdate* End = Find(Updates, 1);
	if (End == nullptr)
	{
		AddError(TEXT("2 本目が所要時間で終わらなかった"));
		return false;
	}
	TestTrue(TEXT("2 本目の開始から所要時間で終わる"), End->bCleared);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapMotionZeroOffsetClearsImmediatelyTest,
	"CUBELITH.Render.SnapMotion.ZeroOffsetClearsImmediately",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapMotionZeroOffsetClearsImmediatelyTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::SnapMotionTestDetail;

	// ずれ 0 で Start したら補間する物が無い ＝ 即座にずれ無し（TS の start の先頭の分岐）
	Cubelith::FSnapMotion Motion(Duration);
	TArray<Cubelith::FSnapOffsetUpdate> Updates;

	// 走っていないピースにずれ 0 で Start しても、書き戻す物が無いので何も出ない
	Motion.Start(2, Cubelith::Vec3(0, 0, 0), 0.0, Updates);
	TestEqual(TEXT("走っていなければ何も出ない"), Updates.Num(), 0);
	TestFalse(TEXT("補間は走らない"), Motion.IsRunning(2));

	// 走っている途中にずれ 0 で Start したら、そこで打ち切ってずれ無しに戻す
	Motion.Start(2, Cubelith::Vec3(0, -1, 0), 0.0, Updates);
	TestTrue(TEXT("補間が走った"), Motion.IsRunning(2));
	Motion.Start(2, Cubelith::Vec3(0, 0, 0), Duration * 0.25, Updates);
	if (Updates.Num() != 1)
	{
		AddError(FString::Printf(TEXT("ずれ 0 の Start が出した件数: 期待 1 / 実際 %d"), Updates.Num()));
		return false;
	}
	TestTrue(TEXT("ずれ無しに戻る"), Updates[0].bCleared);
	TestFalse(TEXT("補間は走っていない"), Motion.IsRunning(2));

	// Cancel も同じで、走っていなければ何も出ない・走っていればずれ無しに戻す
	Motion.Cancel(2, Updates);
	TestEqual(TEXT("走っていない Cancel は何も出さない"), Updates.Num(), 0);

	Motion.Start(4, Cubelith::Vec3(1, 1, 0), 0.0, Updates);
	Motion.Start(5, Cubelith::Vec3(0, 0, 1), 0.0, Updates);
	Motion.Cancel(4, Updates);
	if (Updates.Num() != 1)
	{
		AddError(FString::Printf(TEXT("Cancel が出した件数: 期待 1 / 実際 %d"), Updates.Num()));
		return false;
	}
	TestEqual(TEXT("打ち切ったピース id"), Updates[0].PieceId, 4);
	TestTrue(TEXT("ずれ無しに戻る"), Updates[0].bCleared);
	TestEqual(TEXT("もう 1 本は走ったまま"), Motion.Num(), 1);

	// CancelAll は走っている分をまとめて畳む
	Motion.Start(6, Cubelith::Vec3(-1, 0, 0), 0.0, Updates);
	Motion.CancelAll(Updates);
	TestEqual(TEXT("CancelAll が出した件数"), Updates.Num(), 2);
	TestEqual(TEXT("走っている本数"), Motion.Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
