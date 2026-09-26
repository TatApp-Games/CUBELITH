// 2 本指ジェスチャの認識（移植元 WebMock/src/input/twoFingerGesture.ts）

#include "CubelithTwoFingerGesture.h"

namespace Cubelith
{
	namespace
	{
		/** 何も起きていないことを表す戻り値（TS の NONE） */
		FTwoFingerAction NoneAction()
		{
			return FTwoFingerAction{};
		}

		/** ピンチの戻り値 */
		FTwoFingerAction ZoomAction(double Scale)
		{
			FTwoFingerAction Action;
			Action.Kind = ETwoFingerActionKind::Zoom;
			Action.Scale = Scale;
			return Action;
		}

		/** 90 度回転の戻り値 */
		FTwoFingerAction RotateAction(ETwoFingerRotateGesture Gesture, int32 Dir)
		{
			FTwoFingerAction Action;
			Action.Kind = ETwoFingerActionKind::Rotate;
			Action.Gesture = Gesture;
			Action.Dir = Dir;
			return Action;
		}

		/** 2 点間の距離（TS の Math.hypot） */
		double Distance(const FVector2D& A, const FVector2D& B)
		{
			return FMath::Sqrt((B.X - A.X) * (B.X - A.X) + (B.Y - A.Y) * (B.Y - A.Y));
		}
	}

	double NormalizeAngle(double Radians)
	{
		constexpr double TwoPi = UE_DOUBLE_PI * 2.0;
		// JS の % も C の fmod も「符号は割られる側」なので、負の値を足して回す TS の式がそのまま使える
		const double Wrapped = FMath::Fmod(FMath::Fmod(Radians + UE_DOUBLE_PI, TwoPi) + TwoPi, TwoPi);
		return Wrapped - UE_DOUBLE_PI;
	}

	void FTwoFingerGesture::Anchor(const FVector2D& A, const FVector2D& B)
	{
		StartDistance = Distance(A, B);
		LastDistance = StartDistance;
		StartAngle = FMath::Atan2(B.Y - A.Y, B.X - A.X);
		StartCenterX = (A.X + B.X) / 2.0;
		StartCenterY = (A.Y + B.Y) / 2.0;
	}

	void FTwoFingerGesture::Reset(const FVector2D& A, const FVector2D& B)
	{
		Mode = EMode::Idle;
		Anchor(A, B);
	}

	FTwoFingerAction FTwoFingerGesture::Update(const FVector2D& A, const FVector2D& B)
	{
		if (Mode == EMode::Done)
		{
			return NoneAction();
		}

		const double CurrentDistance = Distance(A, B);
		if (Mode == EMode::Zoom)
		{
			if (CurrentDistance <= 0.0 || LastDistance <= 0.0)
			{
				return NoneAction();
			}
			const double Scale = LastDistance / CurrentDistance;
			LastDistance = CurrentDistance;
			return ZoomAction(Scale);
		}

		if (StartDistance <= 0.0 || CurrentDistance <= 0.0)
		{
			return NoneAction();
		}

		// 3 つの候補を「閾値に対する進み具合」に正規化して比べる
		const double Pinch = FMath::Abs(CurrentDistance / StartDistance - 1.0) / PinchRatioThreshold;
		const double TwistAngle = NormalizeAngle(FMath::Atan2(B.Y - A.Y, B.X - A.X) - StartAngle);
		const double Twist = FMath::Abs(TwistAngle) / TwistRadiansThreshold;
		const double Dx = (A.X + B.X) / 2.0 - StartCenterX;
		const double Dy = (A.Y + B.Y) / 2.0 - StartCenterY;
		const double Swipe = FMath::Sqrt(Dx * Dx + Dy * Dy) / SwipePixelsThreshold;

		if (Pinch < 1.0 && Twist < 1.0 && Swipe < 1.0)
		{
			return NoneAction();
		}

		if (Pinch >= Twist && Pinch >= Swipe)
		{
			Mode = EMode::Zoom;
			const double Scale = LastDistance / CurrentDistance;
			LastDistance = CurrentDistance;
			return ZoomAction(Scale);
		}

		Mode = EMode::Done;
		if (Twist >= Swipe)
		{
			// 画面座標は Y が下向きなので、角度が増える向き = 画面上の時計回り
			return RotateAction(ETwoFingerRotateGesture::Roll, TwistAngle > 0.0 ? 1 : -1);
		}
		if (FMath::Abs(Dx) >= FMath::Abs(Dy))
		{
			return RotateAction(ETwoFingerRotateGesture::Yaw, Dx > 0.0 ? 1 : -1);
		}
		// 上へスワイプ（画面座標の Y が減る）が +1
		return RotateAction(ETwoFingerRotateGesture::Pitch, Dy < 0.0 ? 1 : -1);
	}
}
