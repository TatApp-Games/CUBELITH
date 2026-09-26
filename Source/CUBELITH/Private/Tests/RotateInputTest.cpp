// CubelithRotateInput（2 本指 → グリッド軸まわりの 90 度、ドラッグ → トラックボール回転。Docs/RULES.md 3.3）のテスト。
// 移植元は WebMock/src/input/pieceInput.ts の applyTwoFinger / updateRotateDrag で、あちらにテストは無い
// （DOM のイベントに埋まっているため）。UE 版では純粋関数に切り出したので、間違えやすい 2 点をここで固定する:
//   1. 向きの符号（screenSign とグリッド軸の符号の掛け合わせ）
//   2. 座標系の写し替え（Docs/SPEC_UE.md 7.2 の置換で角度の符号が反転すること）
// 2 は「見たままどう回るか」（手前の面が右へ / 上の面が奥へ）と、90 度ぶん回したときに
// Cubelith::SnappedOrientation が 2 本指と同じ向きへ落ちることの両方で見ている。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithAxisMapping.h"
#include "CubelithFreeRotation.h"
#include "CubelithRotateInput.h"
#include "CubelithTwoFingerGesture.h"
#include "Grid.h"

namespace CubelithRenderTests
{
	namespace
	{
		/** ベクトルの比較の許容差（回転は 90 度ぶんなので、丸め誤差だけを吸収できれば足りる） */
		constexpr double RotateInputTolerance = 1.0e-9;

		/** 名前は Unity ビルドで他のテストと混ざらないよう RotateInput を冠してある */
		const TCHAR* RotateInputAxisName(Cubelith::EAxis Axis)
		{
			switch (Axis)
			{
			case Cubelith::EAxis::X: return TEXT("x");
			case Cubelith::EAxis::Y: return TEXT("y");
			default: return TEXT("z");
			}
		}

		Cubelith::FAxisStep RotateInputStep(Cubelith::EAxis Axis, int32 Sign)
		{
			Cubelith::FAxisStep Step;
			Step.Axis = Axis;
			Step.Sign = Sign;
			return Step;
		}

		Cubelith::FTwoFingerAction RotateInputRotateAction(Cubelith::ETwoFingerRotateGesture Gesture, int32 Dir)
		{
			Cubelith::FTwoFingerAction Action;
			Action.Kind = Cubelith::ETwoFingerActionKind::Rotate;
			Action.Gesture = Gesture;
			Action.Dir = Dir;
			return Action;
		}

		/**
		 * UE ワールドの基底が恒等（カメラが +X を向き、+Y が右・+Z が上）のときのドラッグ軸。
		 * 7.2 の入れ替え（UE の (X, Y, Z) = ロジックの (x, z, y)）を通すと
		 * 画面の右 = ロジック +z、画面の上 = ロジック +y、奥 = ロジック +x になる
		 * （ACubelithPlayerController::ComputeDragAxes が実際にこの変換をしている）
		 */
		Cubelith::FDragAxes RotateInputIdentityAxes()
		{
			Cubelith::FDragAxes Axes;
			Axes.Right = RotateInputStep(Cubelith::EAxis::Z, 1);
			Axes.Up = RotateInputStep(Cubelith::EAxis::Y, 1);
			Axes.Depth = RotateInputStep(Cubelith::EAxis::X, 1);
			return Axes;
		}

		/** 上の基底の裏側（カメラが反対から見ている）。3 軸とも符号が反転する */
		Cubelith::FDragAxes RotateInputFlippedAxes()
		{
			Cubelith::FDragAxes Axes;
			Axes.Right = RotateInputStep(Cubelith::EAxis::Z, -1);
			Axes.Up = RotateInputStep(Cubelith::EAxis::Y, -1);
			Axes.Depth = RotateInputStep(Cubelith::EAxis::X, -1);
			return Axes;
		}
	}
}

// 1. ピンチと「何も起きていない」は回転ではない（Dir = 0 で返る）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithRotateInputNonRotateTest, "CUBELITH.Render.RotateInput.NonRotate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithRotateInputNonRotateTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	const Cubelith::FDragAxes Axes = RotateInputIdentityAxes();

	Cubelith::FTwoFingerAction NoneAction;
	if (Cubelith::TwoFingerRotateStep(NoneAction, Axes).Dir != 0)
	{
		AddError(TEXT("Kind == None で回転が返った"));
	}

	Cubelith::FTwoFingerAction ZoomAction;
	ZoomAction.Kind = Cubelith::ETwoFingerActionKind::Zoom;
	ZoomAction.Scale = 0.8;
	if (Cubelith::TwoFingerRotateStep(ZoomAction, Axes).Dir != 0)
	{
		AddError(TEXT("Kind == Zoom で回転が返った"));
	}

	return !HasAnyErrors();
}

// 2. 画面基準の軸の割り当てと符号（TS の applyTwoFinger の step と screenSign）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithRotateInputScreenAxesTest, "CUBELITH.Render.RotateInput.ScreenAxes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithRotateInputScreenAxesTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	struct FCase
	{
		Cubelith::ETwoFingerRotateGesture Gesture;
		int32 Dir;
		Cubelith::EAxis ExpectedAxis;
		int32 ExpectedDir;
		const TCHAR* Description;
	};

	// 3 軸とも正を向いている基底（画面の右 = +z、上 = +y、奥 = +x）
	const Cubelith::FDragAxes Axes = RotateInputIdentityAxes();
	const FCase Cases[] = {
		// 右へスワイプ → 画面の上軸まわりに +（手前の面が右へ）
		{ Cubelith::ETwoFingerRotateGesture::Yaw, 1, Cubelith::EAxis::Y, 1, TEXT("右へスワイプ") },
		{ Cubelith::ETwoFingerRotateGesture::Yaw, -1, Cubelith::EAxis::Y, -1, TEXT("左へスワイプ") },
		// 上へスワイプ → 画面の右軸まわりに −（上の面が奥へ）。ここだけ screenSign が反転する
		{ Cubelith::ETwoFingerRotateGesture::Pitch, 1, Cubelith::EAxis::Z, -1, TEXT("上へスワイプ") },
		{ Cubelith::ETwoFingerRotateGesture::Pitch, -1, Cubelith::EAxis::Z, 1, TEXT("下へスワイプ") },
		// 時計回りにひねる → 画面の奥軸まわりに +（画面上でも時計回り）
		{ Cubelith::ETwoFingerRotateGesture::Roll, 1, Cubelith::EAxis::X, 1, TEXT("時計回りにひねる") },
		{ Cubelith::ETwoFingerRotateGesture::Roll, -1, Cubelith::EAxis::X, -1, TEXT("反時計回りにひねる") },
	};

	for (const FCase& Case : Cases)
	{
		const Cubelith::FRotateStep Actual =
			Cubelith::TwoFingerRotateStep(RotateInputRotateAction(Case.Gesture, Case.Dir), Axes);
		if (Actual.Axis != Case.ExpectedAxis || Actual.Dir != Case.ExpectedDir)
		{
			AddError(FString::Printf(TEXT("%s が {%s, %+d}（期待 {%s, %+d}）"),
				Case.Description,
				RotateInputAxisName(Actual.Axis), Actual.Dir,
				RotateInputAxisName(Case.ExpectedAxis), Case.ExpectedDir));
		}
	}

	return !HasAnyErrors();
}

// 3. グリッド軸が負を向いている（カメラが裏から見ている）なら回る向きも反転する
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithRotateInputFlippedAxesTest, "CUBELITH.Render.RotateInput.FlippedAxes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithRotateInputFlippedAxesTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	const Cubelith::FDragAxes Axes = RotateInputFlippedAxes();

	const Cubelith::ETwoFingerRotateGesture Gestures[3] = {
		Cubelith::ETwoFingerRotateGesture::Yaw,
		Cubelith::ETwoFingerRotateGesture::Pitch,
		Cubelith::ETwoFingerRotateGesture::Roll,
	};

	for (const Cubelith::ETwoFingerRotateGesture Gesture : Gestures)
	{
		for (const int32 Dir : { 1, -1 })
		{
			const Cubelith::FRotateStep Flipped =
				Cubelith::TwoFingerRotateStep(RotateInputRotateAction(Gesture, Dir), Axes);
			const Cubelith::FRotateStep Straight =
				Cubelith::TwoFingerRotateStep(RotateInputRotateAction(Gesture, Dir), RotateInputIdentityAxes());

			// 軸は同じで向きだけが反転する
			if (Flipped.Axis != Straight.Axis || Flipped.Dir != -Straight.Dir)
			{
				AddError(FString::Printf(TEXT("符号を反転した基底で {%s, %+d}（期待 {%s, %+d}）"),
					RotateInputAxisName(Flipped.Axis), Flipped.Dir,
					RotateInputAxisName(Straight.Axis), -Straight.Dir));
			}
		}
	}

	return !HasAnyErrors();
}

// 4. トラックボール回転: 動かしていない / 基底が縮退しているなら恒等
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithRotateInputTrackballIdentityTest, "CUBELITH.Render.RotateInput.TrackballIdentity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithRotateInputTrackballIdentityTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	const FVector Right = FVector::YAxisVector;
	const FVector Up = FVector::ZAxisVector;

	if (!Cubelith::TrackballRotation(Right, Up, 0.0, 0.0).Equals(FQuat::Identity))
	{
		AddError(TEXT("動かしていないのに回転が返った"));
	}

	if (!Cubelith::TrackballRotation(Right, Up, 300.0, -200.0, /*DegreesPerPixel=*/0.0).Equals(FQuat::Identity))
	{
		AddError(TEXT("感度 0 なのに回転が返った"));
	}

	if (!Cubelith::TrackballRotation(FVector::ZeroVector, Up, 300.0, 0.0).Equals(FQuat::Identity))
	{
		AddError(TEXT("右ベクトルが縮退しているのに回転が返った"));
	}

	if (!Cubelith::TrackballRotation(Right, FVector::ZeroVector, 300.0, 0.0).Equals(FQuat::Identity))
	{
		AddError(TEXT("上ベクトルが縮退しているのに回転が返った"));
	}

	return !HasAnyErrors();
}

// 5. 「見たまま回る」こと: 右へドラッグ → 手前の面が画面の右へ、上へドラッグ → 上の面が奥へ
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithRotateInputTrackballScreenTest, "CUBELITH.Render.RotateInput.TrackballScreen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithRotateInputTrackballScreenTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	// UE のカメラは自分の +X を向き、+Y が右・+Z が上（回転が恒等のときの基底）
	const FVector Forward = FVector::XAxisVector;
	const FVector Right = FVector::YAxisVector;
	const FVector Up = FVector::ZAxisVector;
	// 90 度回すのに必要なドラッグ量（既定の感度 0.4 度/px）
	const double PixelsPerQuarterTurn = 90.0 / Cubelith::RotateDegreesPerPixel;

	// 右へドラッグ: 手前（カメラ側 = -Forward）の面が画面の右へ回る
	{
		const FQuat Quat = Cubelith::TrackballRotation(Right, Up, PixelsPerQuarterTurn, 0.0);
		const FVector Actual = Quat.RotateVector(-Forward);
		if (!Actual.Equals(Right, RotateInputTolerance))
		{
			AddError(FString::Printf(TEXT("右へドラッグしたとき手前の面が %s へ向いた（期待 %s = 画面の右）"),
				*Actual.ToString(), *Right.ToString()));
		}
	}

	// 上へドラッグ（画面座標の Y は下向きが正なので負の量）: 上の面が奥へ回る
	{
		const FQuat Quat = Cubelith::TrackballRotation(Right, Up, 0.0, -PixelsPerQuarterTurn);
		const FVector Actual = Quat.RotateVector(Up);
		if (!Actual.Equals(Forward, RotateInputTolerance))
		{
			AddError(FString::Printf(TEXT("上へドラッグしたとき上の面が %s へ向いた（期待 %s = 奥）"),
				*Actual.ToString(), *Forward.ToString()));
		}
	}

	return !HasAnyErrors();
}

// 6. 90 度ぶんのドラッグは、2 本指の同じ向きのジェスチャと同じ向き id へ落ちる
//    （回転モードのドラッグと 2 本指スワイプで「同じ方向に回る」ことの保証）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithRotateInputTrackballSnapTest, "CUBELITH.Render.RotateInput.TrackballSnap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithRotateInputTrackballSnapTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	const FVector Right = FVector::YAxisVector;
	const FVector Up = FVector::ZAxisVector;
	const Cubelith::FDragAxes Axes = RotateInputIdentityAxes();
	const double PixelsPerQuarterTurn = 90.0 / Cubelith::RotateDegreesPerPixel;

	struct FCase
	{
		double DeltaX;
		double DeltaY;
		Cubelith::ETwoFingerRotateGesture Gesture;
		int32 GestureDir;
		const TCHAR* Description;
	};

	const FCase Cases[] = {
		{ PixelsPerQuarterTurn, 0.0, Cubelith::ETwoFingerRotateGesture::Yaw, 1, TEXT("右へ") },
		{ -PixelsPerQuarterTurn, 0.0, Cubelith::ETwoFingerRotateGesture::Yaw, -1, TEXT("左へ") },
		{ 0.0, -PixelsPerQuarterTurn, Cubelith::ETwoFingerRotateGesture::Pitch, 1, TEXT("上へ") },
		{ 0.0, PixelsPerQuarterTurn, Cubelith::ETwoFingerRotateGesture::Pitch, -1, TEXT("下へ") },
	};

	for (const FCase& Case : Cases)
	{
		const FQuat Quat = Cubelith::TrackballRotation(Right, Up, Case.DeltaX, Case.DeltaY);

		// 2 本指の同じ向きのジェスチャで回した先（FGame::Rotate が通す経路）
		const Cubelith::FRotateStep Step =
			Cubelith::TwoFingerRotateStep(RotateInputRotateAction(Case.Gesture, Case.GestureDir), Axes);
		const int32 Expected =
			Cubelith::RotateOrientation(Cubelith::IdentityOrientation, Step.Axis, Step.Dir);

		// ドラッグを離したときに通す経路（SnappedOrientation → FGame::Place）
		const int32 Actual = Cubelith::SnappedOrientation(Cubelith::IdentityOrientation, Quat);

		if (Actual != Expected)
		{
			AddError(FString::Printf(TEXT("%s 90 度ぶんドラッグしたら向き %d（2 本指の {%s, %+d} は %d）"),
				Case.Description, Actual, RotateInputAxisName(Step.Axis), Step.Dir, Expected));
		}
	}

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
