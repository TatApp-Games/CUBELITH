// CubelithTwoFingerGesture（2 本指ジェスチャの認識。Docs/RULES.md 3.3 / 6 章）のテスト。
// 移植元は WebMock/tests/twoFingerGesture.test.ts。ピンチ / ひねり / 平行スワイプの切り分けと、
// 「回転は 1 回だけ」「ピンチの倍率は前フレームからの比」を確かめる。
// 角度の畳み込み（NormalizeAngle）は TS に単体のテストが無いので C++ 側で足した。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithTwoFingerGesture.h"

namespace CubelithRenderTests
{
	namespace
	{
		/** 2 本指の組（TS の [Point, Point]） */
		struct FFingerPair
		{
			FVector2D A = FVector2D::ZeroVector;
			FVector2D B = FVector2D::ZeroVector;
		};

		/** 中点 (CenterX, CenterY)・間隔 Gap・傾き Angle の 2 本指（TS の fingers） */
		FFingerPair Fingers(double CenterX, double CenterY, double Gap, double Angle = 0.0)
		{
			const double HalfX = (FMath::Cos(Angle) * Gap) / 2.0;
			const double HalfY = (FMath::Sin(Angle) * Gap) / 2.0;

			FFingerPair Pair;
			Pair.A = FVector2D(CenterX - HalfX, CenterY - HalfY);
			Pair.B = FVector2D(CenterX + HalfX, CenterY + HalfY);
			return Pair;
		}

		/** Reset してから 1 回だけ Update した結果（TS の once） */
		Cubelith::FTwoFingerAction Once(const FFingerPair& Start, const FFingerPair& Next)
		{
			Cubelith::FTwoFingerGesture Gesture;
			Gesture.Reset(Start.A, Start.B);
			return Gesture.Update(Next.A, Next.B);
		}

		FString Describe(const Cubelith::FTwoFingerAction& Action)
		{
			switch (Action.Kind)
			{
			case Cubelith::ETwoFingerActionKind::None:
				return TEXT("None");
			case Cubelith::ETwoFingerActionKind::Zoom:
				return FString::Printf(TEXT("Zoom(scale=%f)"), Action.Scale);
			default:
				break;
			}

			const TCHAR* GestureName = TEXT("Roll");
			if (Action.Gesture == Cubelith::ETwoFingerRotateGesture::Yaw)
			{
				GestureName = TEXT("Yaw");
			}
			else if (Action.Gesture == Cubelith::ETwoFingerRotateGesture::Pitch)
			{
				GestureName = TEXT("Pitch");
			}
			return FString::Printf(TEXT("Rotate(%s, %+d)"), GestureName, Action.Dir);
		}

		bool IsRotate(const Cubelith::FTwoFingerAction& Action, Cubelith::ETwoFingerRotateGesture Gesture, int32 Dir)
		{
			return Action.Kind == Cubelith::ETwoFingerActionKind::Rotate
				&& Action.Gesture == Gesture
				&& Action.Dir == Dir;
		}
	}
}

// 1. 閾値に届かない動きでは何も起きない
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTwoFingerGestureBelowThresholdTest, "CUBELITH.Render.TwoFingerGesture.BelowThreshold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTwoFingerGestureBelowThresholdTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	const FFingerPair Start = Fingers(200.0, 300.0, 100.0);

	// 動いていなければ何も起きない
	const Cubelith::FTwoFingerAction Still = Once(Start, Start);
	if (Still.Kind != Cubelith::ETwoFingerActionKind::None)
	{
		AddError(FString::Printf(TEXT("動いていないのに %s が出た"), *Describe(Still)));
	}

	// 閾値未満の動きでは何も起きない
	const FFingerPair Small = Fingers(200.0 + Cubelith::SwipePixelsThreshold * 0.5, 300.0, 100.0);
	const Cubelith::FTwoFingerAction SmallAction = Once(Start, Small);
	if (SmallAction.Kind != Cubelith::ETwoFingerActionKind::None)
	{
		AddError(FString::Printf(TEXT("閾値未満の動きで %s が出た"), *Describe(SmallAction)));
	}

	return !HasAnyErrors();
}

// 2. ピンチ: 広げると寄る（Scale < 1）・狭めると引く（Scale > 1）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTwoFingerGesturePinchTest, "CUBELITH.Render.TwoFingerGesture.Pinch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTwoFingerGesturePinchTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	const FFingerPair Start = Fingers(200.0, 300.0, 100.0);

	const FFingerPair Spread = Fingers(200.0, 300.0, 100.0 * (1.0 + Cubelith::PinchRatioThreshold * 2.0));
	const Cubelith::FTwoFingerAction SpreadAction = Once(Start, Spread);
	if (SpreadAction.Kind != Cubelith::ETwoFingerActionKind::Zoom || SpreadAction.Scale >= 1.0)
	{
		AddError(FString::Printf(TEXT("指を広げたのに %s（期待 Zoom で Scale < 1）"), *Describe(SpreadAction)));
	}

	const FFingerPair Narrow = Fingers(200.0, 300.0, 100.0 * (1.0 - Cubelith::PinchRatioThreshold * 2.0));
	const Cubelith::FTwoFingerAction NarrowAction = Once(Start, Narrow);
	if (NarrowAction.Kind != Cubelith::ETwoFingerActionKind::Zoom || NarrowAction.Scale <= 1.0)
	{
		AddError(FString::Printf(TEXT("指を狭めたのに %s（期待 Zoom で Scale > 1）"), *Describe(NarrowAction)));
	}

	return !HasAnyErrors();
}

// 3. 平行スワイプ（Yaw / Pitch）とひねり（Roll）の向き
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTwoFingerGestureRotateTest, "CUBELITH.Render.TwoFingerGesture.Rotate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTwoFingerGestureRotateTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;
	using Cubelith::ETwoFingerRotateGesture;

	const FFingerPair Start = Fingers(200.0, 300.0, 100.0);
	const double Swipe = Cubelith::SwipePixelsThreshold * 2.0;
	const double Twist = Cubelith::TwistRadiansThreshold * 2.0;

	struct FCase
	{
		const TCHAR* Name;
		FFingerPair Next;
		ETwoFingerRotateGesture Gesture;
		int32 Dir;
	};

	const FCase Cases[] = {
		{ TEXT("右へ平行スワイプ"), Fingers(200.0 + Swipe, 300.0, 100.0), ETwoFingerRotateGesture::Yaw, 1 },
		{ TEXT("左へ平行スワイプ"), Fingers(200.0 - Swipe, 300.0, 100.0), ETwoFingerRotateGesture::Yaw, -1 },
		// 画面座標は Y が下向きなので、上へスワイプ = Y が減る
		{ TEXT("上へ平行スワイプ"), Fingers(200.0, 300.0 - Swipe, 100.0), ETwoFingerRotateGesture::Pitch, 1 },
		{ TEXT("下へ平行スワイプ"), Fingers(200.0, 300.0 + Swipe, 100.0), ETwoFingerRotateGesture::Pitch, -1 },
		{ TEXT("時計回りのひねり"), Fingers(200.0, 300.0, 100.0, Twist), ETwoFingerRotateGesture::Roll, 1 },
		{ TEXT("反時計回りのひねり"), Fingers(200.0, 300.0, 100.0, -Twist), ETwoFingerRotateGesture::Roll, -1 },
	};

	for (const FCase& Case : Cases)
	{
		const Cubelith::FTwoFingerAction Action = Once(Start, Case.Next);
		if (!IsRotate(Action, Case.Gesture, Case.Dir))
		{
			AddError(FString::Printf(TEXT("%s: %s が出た"), Case.Name, *Describe(Action)));
		}
	}

	return !HasAnyErrors();
}

// 4. 回転はひと続きのジェスチャで 1 回だけ。指を置き直せばまた回せる
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTwoFingerGestureRotateOnceTest, "CUBELITH.Render.TwoFingerGesture.RotateOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTwoFingerGestureRotateOnceTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;
	using Cubelith::ETwoFingerRotateGesture;

	Cubelith::FTwoFingerGesture Gesture;
	const FFingerPair Start = Fingers(200.0, 300.0, 100.0);
	Gesture.Reset(Start.A, Start.B);

	const FFingerPair First = Fingers(200.0 + Cubelith::SwipePixelsThreshold * 2.0, 300.0, 100.0);
	const Cubelith::FTwoFingerAction FirstAction = Gesture.Update(First.A, First.B);
	if (!IsRotate(FirstAction, ETwoFingerRotateGesture::Yaw, 1))
	{
		AddError(FString::Printf(TEXT("1 回目が %s（期待 Rotate(Yaw, +1)）"), *Describe(FirstAction)));
	}

	// スワイプし続けても回り続けない
	for (int32 StepIndex = 3; StepIndex < 10; ++StepIndex)
	{
		const FFingerPair Next = Fingers(200.0 + Cubelith::SwipePixelsThreshold * StepIndex, 300.0, 100.0);
		const Cubelith::FTwoFingerAction Action = Gesture.Update(Next.A, Next.B);
		if (Action.Kind != Cubelith::ETwoFingerActionKind::None)
		{
			AddError(FString::Printf(TEXT("%d 段目で %s が出た（打ち止めにならなかった）"), StepIndex, *Describe(Action)));
		}
	}

	// 指を置き直せばまた回せる
	Gesture.Reset(Start.A, Start.B);
	const Cubelith::FTwoFingerAction Again = Gesture.Update(First.A, First.B);
	if (Again.Kind != Cubelith::ETwoFingerActionKind::Rotate)
	{
		AddError(FString::Printf(TEXT("Reset 後に %s（期待 Rotate）"), *Describe(Again)));
	}

	return !HasAnyErrors();
}

// 5. モードの固定と倍率の積み上げ: ピンチと判定したら回転は出ない・Scale を掛け合わせると全体の倍率になる
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTwoFingerGestureZoomModeTest, "CUBELITH.Render.TwoFingerGesture.ZoomMode",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTwoFingerGestureZoomModeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	{
		// ピンチと判定したらそのまま続き、途中で大きく平行移動しても回らない
		Cubelith::FTwoFingerGesture Gesture;
		const FFingerPair Start = Fingers(200.0, 300.0, 100.0);
		Gesture.Reset(Start.A, Start.B);

		const FFingerPair Pinched = Fingers(200.0, 300.0, 130.0);
		const Cubelith::FTwoFingerAction First = Gesture.Update(Pinched.A, Pinched.B);
		if (First.Kind != Cubelith::ETwoFingerActionKind::Zoom)
		{
			AddError(FString::Printf(TEXT("ピンチが %s になった"), *Describe(First)));
		}

		const FFingerPair Moved = Fingers(600.0, 300.0, 130.0);
		const Cubelith::FTwoFingerAction Second = Gesture.Update(Moved.A, Moved.B);
		if (Second.Kind != Cubelith::ETwoFingerActionKind::Zoom)
		{
			AddError(FString::Printf(TEXT("ピンチ中の平行移動で %s が出た（期待 Zoom）"), *Describe(Second)));
		}
	}

	{
		// 前フレームからの変化を返すので、掛け合わせると全体の倍率（100 / 200）になる
		Cubelith::FTwoFingerGesture Gesture;
		const FFingerPair Start = Fingers(200.0, 300.0, 100.0);
		Gesture.Reset(Start.A, Start.B);

		const double Gaps[] = { 130.0, 160.0, 200.0 };
		double Total = 1.0;
		for (const double Gap : Gaps)
		{
			const FFingerPair Next = Fingers(200.0, 300.0, Gap);
			const Cubelith::FTwoFingerAction Action = Gesture.Update(Next.A, Next.B);
			if (Action.Kind != Cubelith::ETwoFingerActionKind::Zoom)
			{
				AddError(FString::Printf(TEXT("間隔 %f で %s が出た（期待 Zoom）"), Gap, *Describe(Action)));
				return false;
			}
			Total *= Action.Scale;
		}

		if (!FMath::IsNearlyEqual(Total, 100.0 / 200.0, 1.0e-10))
		{
			AddError(FString::Printf(TEXT("倍率の積が %f（期待 %f）"), Total, 100.0 / 200.0));
		}
	}

	return !HasAnyErrors();
}

// 6. 角度の畳み込み（TS の normalizeAngle と同じ結果になること）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTwoFingerGestureNormalizeAngleTest, "CUBELITH.Render.TwoFingerGesture.NormalizeAngle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTwoFingerGestureNormalizeAngleTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	constexpr double TwoPi = UE_DOUBLE_PI * 2.0;

	struct FCase
	{
		double Input;
		double Expected;
	};

	const FCase Cases[] = {
		{ 0.0, 0.0 },
		{ 0.5, 0.5 },
		{ -0.5, -0.5 },
		// 式の上では PI は -PI へ落ちる（範囲は [-PI, PI)）
		{ UE_DOUBLE_PI, -UE_DOUBLE_PI },
		{ -UE_DOUBLE_PI, -UE_DOUBLE_PI },
		// 巻き戻り: 1 周足しても引いても同じ値になる
		{ 0.3 + TwoPi, 0.3 },
		{ 0.3 - TwoPi, 0.3 },
		{ 0.3 + TwoPi * 3.0, 0.3 },
		// PI をまたぐと反対側へ回り込む（ひねりの巻き戻りが消える）
		{ UE_DOUBLE_PI + 0.2, -UE_DOUBLE_PI + 0.2 },
		{ -UE_DOUBLE_PI - 0.2, UE_DOUBLE_PI - 0.2 },
	};

	for (const FCase& Case : Cases)
	{
		const double Actual = Cubelith::NormalizeAngle(Case.Input);
		if (!FMath::IsNearlyEqual(Actual, Case.Expected, 1.0e-9))
		{
			AddError(FString::Printf(TEXT("NormalizeAngle(%f) が %f（期待 %f）"), Case.Input, Actual, Case.Expected));
		}
	}

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
