// CubelithFreeRotation（自由回転 → 最寄りの 90 度の向き id。Docs/RULES.md 3.3）のテスト。
// 移植元は WebMock/tests/freeRotation.test.ts。列優先 / 行優先の取り違えに加えて、
// UE 版では座標系の写し替え（Docs/SPEC_UE.md 7.2 の置換 P）も入るので取り違えが起きやすい。
// そこを固定するために、既存の Cubelith::OrientationToWorldQuat（CubelithCoords）との整合を
// 24 × 24 通りで確かめる Compose のテストを土台に置き、その上で任意角のケースを見ている。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithCoords.h"
#include "CubelithFreeRotation.h"
#include "Grid.h"

namespace CubelithRenderTests
{
	// FreeRotationTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、
	// 名前をこの中に閉じる（素の無名 namespace だと CubelithPlayerController.cpp の同名の関数と衝突する）。
	// **LogicAxisName は呼ぶときも名前空間で修飾する**: 名前空間に入れても、あちらの無名名前空間の
	// 同名のヘルパは大域スコープに見えているので、テストが `using namespace` を書いた先での
	// 修飾なしの呼び出しは「どちらか決められない」（C2668）になる
	namespace FreeRotationTestDetail
	{
		/** 向きの比較は id なので厳密。行列の要素の比較だけ許容差を付ける */
		constexpr double FreeRotationTolerance = 1.0e-9;

		/**
		 * ロジック座標の軸まわりに Degrees 度回すクォータニオンを、**UE ワールドの**クォータニオンとして作る
		 * （TS の quaternionAround に対応）。
		 *
		 * ロジック軸 a まわり角 θ の回転 R を UE へ写すと R_UE = P · R · P で、P は置換（det = -1）なので
		 *   P · [a]x · P = -[P a]x
		 * より「軸 P a まわり角 -θ」になる。P はロジックの (x, y, z) を UE の (x, z, y) へ写すので
		 * ロジック x → UE X、ロジック y → UE Z、ロジック z → UE Y。角度は符号が反転する。
		 * FQuat の軸角はロジックでも UE でも数値上は右手系なので、この軸と符号だけ入れ替えればよい。
		 * 90 度の場合にこの作り方が OrientationToWorldQuat(RotateOrientation(...)) と一致することは
		 * 下の AxisQuaternion テストで確かめている。
		 */
		FQuat QuatAroundLogicAxis(Cubelith::EAxis Axis, double Degrees)
		{
			FVector WorldAxis = FVector::XAxisVector;
			if (Axis == Cubelith::EAxis::Y)
			{
				WorldAxis = FVector::ZAxisVector;
			}
			else if (Axis == Cubelith::EAxis::Z)
			{
				WorldAxis = FVector::YAxisVector;
			}
			return FQuat(WorldAxis, FMath::DegreesToRadians(-Degrees));
		}

		const TCHAR* LogicAxisName(Cubelith::EAxis Axis)
		{
			switch (Axis)
			{
			case Cubelith::EAxis::X: return TEXT("x");
			case Cubelith::EAxis::Y: return TEXT("y");
			default: return TEXT("z");
			}
		}

		/** 確かめに使う 3 軸 */
		constexpr Cubelith::EAxis LogicAxes[3] = { Cubelith::EAxis::X, Cubelith::EAxis::Y, Cubelith::EAxis::Z };
	}
}

// 1. 行列の並べ替え: 恒等は恒等・OrientationToWorldMatrix の逆変換になっている
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithFreeRotationRowMajorTest, "CUBELITH.Render.FreeRotation.RowMajor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithFreeRotationRowMajorTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::FreeRotationTestDetail;

	const TArray<double> Identity = Cubelith::WorldRotationToLogicRowMajor(FMatrix::Identity);
	const double ExpectedIdentity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	if (Identity.Num() != 9)
	{
		AddError(FString::Printf(TEXT("戻り値の要素数が %d（期待 9）"), Identity.Num()));
		return false;
	}
	for (int32 Index = 0; Index < 9; ++Index)
	{
		if (!FMath::IsNearlyEqual(Identity[Index], ExpectedIdentity[Index], FreeRotationTolerance))
		{
			AddError(FString::Printf(TEXT("単位行列の %d 要素目が %f（期待 %f）"),
				Index, Identity[Index], ExpectedIdentity[Index]));
		}
	}

	// 24 通りすべてで OrientationToWorldMatrix（ロジック → UE）の逆変換になっていること
	for (int32 Orientation = 0; Orientation < Cubelith::OrientationCount; ++Orientation)
	{
		const TArray<double> Actual =
			Cubelith::WorldRotationToLogicRowMajor(Cubelith::OrientationToWorldMatrix(Orientation));
		const Cubelith::FMat3 Expected = Cubelith::OrientationMatrix(Orientation);

		for (int32 Index = 0; Index < 9; ++Index)
		{
			if (!FMath::IsNearlyEqual(Actual[Index], static_cast<double>(Expected.M[Index]), FreeRotationTolerance))
			{
				AddError(FString::Printf(TEXT("向き %d の %d 要素目が %f（期待 %d）"),
					Orientation, Index, Actual[Index], Expected.M[Index]));
				return false;
			}
		}
	}

	return !HasAnyErrors();
}

// 2. 自由回転が恒等なら向きは変わらない
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithFreeRotationIdentityTest, "CUBELITH.Render.FreeRotation.Identity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithFreeRotationIdentityTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::FreeRotationTestDetail;

	for (int32 Orientation = 0; Orientation < Cubelith::OrientationCount; ++Orientation)
	{
		const int32 Actual = Cubelith::SnappedOrientation(Orientation, FQuat::Identity);
		if (Actual != Orientation)
		{
			AddError(FString::Printf(TEXT("恒等の自由回転で向き %d が %d になった"), Orientation, Actual));
		}
	}

	return !HasAnyErrors();
}

// 3. 既存の変換との整合: SnappedOrientation(a, OrientationToWorldQuat(b)) == ComposeOrientation(a, b)
//    （TS に無い C++ 側のテスト。a = 0 のときが「24 通りすべてで元の向きに戻る」ことに当たる）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithFreeRotationComposeTest, "CUBELITH.Render.FreeRotation.Compose",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithFreeRotationComposeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::FreeRotationTestDetail;

	for (int32 B = 0; B < Cubelith::OrientationCount; ++B)
	{
		const FQuat Quat = Cubelith::OrientationToWorldQuat(B);

		// 恒等の向きに向き B の回転を掛けると B に戻る
		const int32 FromIdentity = Cubelith::SnappedOrientation(Cubelith::IdentityOrientation, Quat);
		if (FromIdentity != B)
		{
			AddError(FString::Printf(TEXT("SnappedOrientation(0, OrientationToWorldQuat(%d)) が %d"), B, FromIdentity));
		}

		for (int32 A = 0; A < Cubelith::OrientationCount; ++A)
		{
			const int32 Expected = Cubelith::ComposeOrientation(A, B);
			const int32 Actual = Cubelith::SnappedOrientation(A, Quat);
			if (Actual != Expected)
			{
				AddError(FString::Printf(TEXT("SnappedOrientation(%d, 向き %d の回転) が %d（期待 %d）"),
					A, B, Actual, Expected));
				// 24 × 24 通りあるので、ずれたら最初の 1 組だけ報告して打ち切る
				return false;
			}
		}
	}

	return !HasAnyErrors();
}

// 4. テストの軸クォータニオンが既存の変換と一致すること（この一致が 5 以降の前提になる）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithFreeRotationAxisQuaternionTest, "CUBELITH.Render.FreeRotation.AxisQuaternion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithFreeRotationAxisQuaternionTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::FreeRotationTestDetail;

	for (const Cubelith::EAxis Axis : LogicAxes)
	{
		for (const int32 Dir : { 1, -1 })
		{
			const FQuat Actual = QuatAroundLogicAxis(Axis, 90.0 * static_cast<double>(Dir));
			const int32 Rotated = Cubelith::RotateOrientation(Cubelith::IdentityOrientation, Axis, Dir);
			const FQuat Expected = Cubelith::OrientationToWorldQuat(Rotated);

			// クォータニオンは q と -q が同じ回転なので、回転としての一致を見る
			if (!Actual.Equals(Expected, KINDA_SMALL_NUMBER) && !Actual.Equals(Expected * -1.0, KINDA_SMALL_NUMBER))
			{
				AddError(FString::Printf(TEXT("軸 %s の %+d 方向 90 度が %s（期待 %s）"),
					CubelithRenderTests::FreeRotationTestDetail::LogicAxisName(Axis), Dir,
					*Actual.ToString(), *Expected.ToString()));
			}
		}
	}

	return !HasAnyErrors();
}

// 5. 軸 ±90 度: どの向きからでも「今の向きのあとにワールド軸で回す」合成になる
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithFreeRotationAxisStepTest, "CUBELITH.Render.FreeRotation.AxisStep",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithFreeRotationAxisStepTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::FreeRotationTestDetail;

	for (int32 Orientation = 0; Orientation < Cubelith::OrientationCount; ++Orientation)
	{
		for (const Cubelith::EAxis Axis : LogicAxes)
		{
			for (const int32 Dir : { 1, -1 })
			{
				const int32 Expected = Cubelith::RotateOrientation(Orientation, Axis, Dir);
				const int32 Actual =
					Cubelith::SnappedOrientation(Orientation, QuatAroundLogicAxis(Axis, 90.0 * static_cast<double>(Dir)));
				if (Actual != Expected)
				{
					AddError(FString::Printf(TEXT("向き %d に軸 %s の %+d 方向 90 度で %d（期待 %d）"),
						Orientation, CubelithRenderTests::FreeRotationTestDetail::LogicAxisName(Axis),
						Dir, Actual, Expected));
					return false;
				}
			}
		}
	}

	return !HasAnyErrors();
}

// 6. スナップの境界: 45 度未満のずれは元の向きへ、45 度を超えると次の向きへ
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithFreeRotationSnapBoundaryTest, "CUBELITH.Render.FreeRotation.SnapBoundary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithFreeRotationSnapBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::FreeRotationTestDetail;

	for (int32 Orientation = 0; Orientation < Cubelith::OrientationCount; ++Orientation)
	{
		const int32 Next = Cubelith::RotateOrientation(Orientation, Cubelith::EAxis::Y, 1);

		struct FCase
		{
			double Degrees;
			int32 Expected;
		};

		const FCase Cases[] = {
			{ 20.0, Orientation },
			{ -20.0, Orientation },
			{ 80.0, Next },
			{ 100.0, Next },
		};

		for (const FCase& Case : Cases)
		{
			const int32 Actual =
				Cubelith::SnappedOrientation(Orientation, QuatAroundLogicAxis(Cubelith::EAxis::Y, Case.Degrees));
			if (Actual != Case.Expected)
			{
				AddError(FString::Printf(TEXT("向き %d に y 軸 %.0f 度で %d（期待 %d）"),
					Orientation, Case.Degrees, Actual, Case.Expected));
				return false;
			}
		}
	}

	return !HasAnyErrors();
}

// 7. 数度の揺れでは向きが変わらない（TS に無い C++ 側のテスト）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithFreeRotationJitterTest, "CUBELITH.Render.FreeRotation.Jitter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithFreeRotationJitterTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::FreeRotationTestDetail;

	// 軸に揃っていない向きの、数度の揺れ
	const FVector JitterAxes[3] = {
		FVector(0.3, -0.5, 0.8).GetSafeNormal(),
		FVector(-0.7, 0.2, 0.68).GetSafeNormal(),
		FVector(0.5, 0.5, -0.7).GetSafeNormal(),
	};
	const double JitterDegrees[3] = { 3.0, -6.0, 9.0 };

	for (int32 Orientation = 0; Orientation < Cubelith::OrientationCount; ++Orientation)
	{
		const FQuat Base = Cubelith::OrientationToWorldQuat(Orientation);

		for (const FVector& JitterAxis : JitterAxes)
		{
			for (const double Degrees : JitterDegrees)
			{
				// FQuat の A * B は「B を先に、次に A」なので、Jitter * Base は
				// 「向き Orientation に置いたあと、あとから Jitter で揺らした」姿勢になる
				const FQuat Jitter(JitterAxis, FMath::DegreesToRadians(Degrees));
				const int32 Actual = Cubelith::SnappedOrientation(Cubelith::IdentityOrientation, Jitter * Base);
				if (Actual != Orientation)
				{
					AddError(FString::Printf(TEXT("向き %d に軸 %s まわり %.0f 度の揺れで %d になった"),
						Orientation, *JitterAxis.ToString(), Degrees, Actual));
					return false;
				}
			}
		}
	}

	return !HasAnyErrors();
}

// 8. 2 軸ぶんのトラックボール回転も 24 通りのどれかへ落ちる（TS の同名のケース）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithFreeRotationTrackballTest, "CUBELITH.Render.FreeRotation.Trackball",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithFreeRotationTrackballTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::FreeRotationTestDetail;

	// 右へ 90 度・上へ 90 度ぶん回したときの合成（掛ける順は表示と同じく左から）
	const FQuat Yaw = QuatAroundLogicAxis(Cubelith::EAxis::Y, 88.0);
	const FQuat Pitch = QuatAroundLogicAxis(Cubelith::EAxis::X, -92.0);
	const FQuat Combined = Yaw * Pitch;

	const int32 Actual = Cubelith::SnappedOrientation(Cubelith::IdentityOrientation, Combined);
	// ComposeOrientation(A, B) は「A を適用してから B」。ワールド軸の回転を重ねる順と同じ
	const int32 Expected = Cubelith::ComposeOrientation(
		Cubelith::RotateOrientation(Cubelith::IdentityOrientation, Cubelith::EAxis::X, -1),
		Cubelith::RotateOrientation(Cubelith::IdentityOrientation, Cubelith::EAxis::Y, 1));

	if (Actual != Expected)
	{
		AddError(FString::Printf(TEXT("トラックボール回転が %d（期待 %d）"), Actual, Expected));
	}

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
