// 回転の操作 → ロジックの言葉（移植元 WebMock/src/input/pieceInput.ts の applyTwoFinger / updateRotateDrag）

#include "CubelithRotateInput.h"

namespace Cubelith
{
	FRotateStep TwoFingerRotateStep(const FTwoFingerAction& Action, const FDragAxes& Axes)
	{
		FRotateStep Result;
		if (Action.Kind != ETwoFingerActionKind::Rotate)
		{
			// ピンチ（ズーム）と「何も起きていない」は回転ではない。Dir = 0 のまま返す
			return Result;
		}

		// 画面基準の軸（Yaw = 画面の上、Pitch = 画面の右、Roll = 画面の奥）をグリッド軸へ写す
		const FAxisStep& Step = (Action.Gesture == ETwoFingerRotateGesture::Yaw)
			? Axes.Up
			: ((Action.Gesture == ETwoFingerRotateGesture::Pitch) ? Axes.Right : Axes.Depth);

		// TS の screenSign。上へスワイプ（Pitch の +1）は画面の右軸まわりの − なので、そこだけ反転する
		const int32 ScreenSign = (Action.Gesture == ETwoFingerRotateGesture::Pitch) ? -Action.Dir : Action.Dir;

		Result.Axis = Step.Axis;
		// 画面の軸がグリッド軸の負の向きを指していれば、グリッド軸まわりの向きは反転する
		Result.Dir = (ScreenSign * Step.Sign > 0) ? 1 : -1;
		return Result;
	}

	FQuat TrackballRotation(
		const FVector& WorldCameraRight,
		const FVector& WorldCameraUp,
		double ScreenDeltaX,
		double ScreenDeltaY,
		double DegreesPerPixel)
	{
		const FVector Right = WorldCameraRight.GetSafeNormal();
		const FVector Up = WorldCameraUp.GetSafeNormal();
		if (Right.IsNearlyZero() || Up.IsNearlyZero())
		{
			// 基底が取れない（縮退）。回さない
			return FQuat::Identity;
		}

		// 角度の符号について。TS はロジックと同じ座標系（Y が上の右手系）でカメラ基底まわりに
		// 「右へドラッグ → 上ベクトルまわりに +」で回している。ロジックの回転 R を UE ワールドへ写すと
		// R_UE = P · R · P（P は y と z を入れ替える置換。Docs/SPEC_UE.md 7.2）で、P は det = -1 なので
		//   P · Rot(a, θ) · P = Rot(P a, -θ)
		// となる（CubelithFreeRotation.h と同じ関係）。引数の基底は既に UE ワールドのベクトル（= P a）なので、
		// 残る違いは角度の符号だけ。だから TS の角度を反転して渡す
		const FQuat YawStep(Up, -FMath::DegreesToRadians(ScreenDeltaX * DegreesPerPixel));
		// 画面の Y は下向きが正なので、上へのドラッグ（ScreenDeltaY < 0）がそのまま右軸まわりの − になる
		const FQuat PitchStep(Right, -FMath::DegreesToRadians(ScreenDeltaY * DegreesPerPixel));

		// FQuat の A * B は「B を先に、次に A」（行列の積と同じ順）。TS の yawStep * pitchStep と揃う。
		// 開始時の姿勢（TS の base）は毎回恒等なので、ここでは掛けない
		return YawStep * PitchStep;
	}
}
