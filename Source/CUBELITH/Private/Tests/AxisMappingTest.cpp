// CubelithAxisMapping（ドラッグ方向 → グリッド軸の写像。Docs/RULES.md 3.3）のテスト。
// 移植元は WebMock/tests/axisMapping.test.ts。ケースはそのまま写し、
// C++ 側で確かめたい点（縮退した入力でも 3 軸が重ならないこと）も同じ並びで見ている。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithAxisMapping.h"
#include "Grid.h"

namespace CubelithRenderTests
{
	namespace
	{
		const TCHAR* AxisName(Cubelith::EAxis Axis)
		{
			switch (Axis)
			{
			case Cubelith::EAxis::X: return TEXT("x");
			case Cubelith::EAxis::Y: return TEXT("y");
			default: return TEXT("z");
			}
		}

		FString Describe(const Cubelith::FAxisStep& Step)
		{
			return FString::Printf(TEXT("{%s, %+d}"), AxisName(Step.Axis), Step.Sign);
		}

		FString DescribeVec(const Cubelith::FVec3& V)
		{
			return FString::Printf(TEXT("(%d, %d, %d)"), V.X, V.Y, V.Z);
		}

		/** 期待値を組み立てる。名前は Unity ビルドで他のテストと混ざらないよう MakeStep にしてある */
		Cubelith::FAxisStep MakeStep(Cubelith::EAxis Axis, int32 Sign)
		{
			Cubelith::FAxisStep Result;
			Result.Axis = Axis;
			Result.Sign = Sign;
			return Result;
		}
	}
}

// 1. 軸に揃ったカメラ / 反対側 / 斜め / 見下ろし の 4 通り（TS の dragAxes の 4 ケース）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithAxisMappingDragAxesTest, "CUBELITH.Render.AxisMapping.DragAxes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithAxisMappingDragAxesTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;
	using Cubelith::EAxis;

	struct FCase
	{
		const TCHAR* Name;
		FVector Right;
		FVector Up;
		FVector Forward;
		Cubelith::FAxisStep ExpectedRight;
		Cubelith::FAxisStep ExpectedUp;
		Cubelith::FAxisStep ExpectedDepth;
	};

	const FCase Cases[] = {
		// +Z から原点を見るカメラ: 右 = +X、上 = +Y、前 = -Z
		{ TEXT("軸に揃ったカメラ"), FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, -1),
			MakeStep(EAxis::X, 1), MakeStep(EAxis::Y, 1), MakeStep(EAxis::Z, -1) },
		// -Z から原点を見るカメラ: 右 = -X、上 = +Y、前 = +Z
		{ TEXT("反対側から見たカメラ"), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1),
			MakeStep(EAxis::X, -1), MakeStep(EAxis::Y, 1), MakeStep(EAxis::Z, 1) },
		// 方位角 30 度ほど回したカメラ。右は X 寄り、前は -Z 寄り
		{ TEXT("斜めのカメラ"), FVector(0.87, 0, -0.5), FVector(0, 1, 0), FVector(-0.5, -0.2, -0.84),
			MakeStep(EAxis::X, 1), MakeStep(EAxis::Y, 1), MakeStep(EAxis::Z, -1) },
		// ほぼ真上から見下ろす: 上ベクトルは -Z 寄り、前は -Y 寄り
		{ TEXT("見下ろすカメラ"), FVector(1, 0, 0), FVector(0, 0.1, -0.99), FVector(0, -0.99, -0.1),
			MakeStep(EAxis::X, 1), MakeStep(EAxis::Z, -1), MakeStep(EAxis::Y, -1) },
	};

	for (const FCase& Case : Cases)
	{
		const Cubelith::FDragAxes Axes = Cubelith::DragAxes(Case.Right, Case.Up, Case.Forward);

		if (Axes.Right != Case.ExpectedRight)
		{
			AddError(FString::Printf(TEXT("%s: Right が %s（期待 %s）"),
				Case.Name, *Describe(Axes.Right), *Describe(Case.ExpectedRight)));
		}
		if (Axes.Up != Case.ExpectedUp)
		{
			AddError(FString::Printf(TEXT("%s: Up が %s（期待 %s）"),
				Case.Name, *Describe(Axes.Up), *Describe(Case.ExpectedUp)));
		}
		if (Axes.Depth != Case.ExpectedDepth)
		{
			AddError(FString::Printf(TEXT("%s: Depth が %s（期待 %s）"),
				Case.Name, *Describe(Axes.Depth), *Describe(Case.ExpectedDepth)));
		}
	}

	return !HasAnyErrors();
}

// 2. どんな向きでも 3 軸が重ならない（縮退した入力・零ベクトルを含む）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithAxisMappingDistinctAxesTest, "CUBELITH.Render.AxisMapping.DistinctAxes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithAxisMappingDistinctAxesTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	struct FSample
	{
		FVector Right;
		FVector Up;
		FVector Forward;
	};

	const FSample Samples[] = {
		{ FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, -1) },
		{ FVector(0.7, 0.7, 0), FVector(-0.7, 0.7, 0), FVector(0, 0, -1) },
		{ FVector(0.6, 0.5, 0.62), FVector(-0.3, 0.86, -0.4), FVector(-0.74, 0, 0.67) },
		// 右と上が同じ軸を向く縮退した入力でも 3 軸に割り当てる
		{ FVector(1, 0, 0), FVector(1, 0, 0), FVector(1, 0, 0) },
		{ FVector(0, 0, 0), FVector(0, 0, 0), FVector(0, 0, 0) },
	};

	for (const FSample& Sample : Samples)
	{
		const Cubelith::FDragAxes Axes = Cubelith::DragAxes(Sample.Right, Sample.Up, Sample.Forward);

		TSet<uint8> Used;
		Used.Add(static_cast<uint8>(Axes.Right.Axis));
		Used.Add(static_cast<uint8>(Axes.Up.Axis));
		Used.Add(static_cast<uint8>(Axes.Depth.Axis));

		if (Used.Num() != 3)
		{
			AddError(FString::Printf(TEXT("基底 (%s / %s / %s) で軸が重なった: %s / %s / %s"),
				*Sample.Right.ToString(), *Sample.Up.ToString(), *Sample.Forward.ToString(),
				*Describe(Axes.Right), *Describe(Axes.Up), *Describe(Axes.Depth)));
		}

		// 成分が 0 の軸は + を既定にする（零ベクトルなら 3 軸すべてが +）
		if (Sample.Right.IsZero() && Axes.Right.Sign != 1)
		{
			AddError(TEXT("零ベクトルの Right の符号が + でない"));
		}
	}

	return !HasAnyErrors();
}

// 3. AxisStepVector が軸と符号どおりのマス数を返す
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithAxisMappingStepVectorTest, "CUBELITH.Render.AxisMapping.StepVector",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithAxisMappingStepVectorTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;
	using Cubelith::EAxis;

	struct FCase
	{
		Cubelith::FAxisStep Step;
		int32 Count;
		Cubelith::FVec3 Expected;
	};

	const FCase Cases[] = {
		{ MakeStep(EAxis::X, 1), 2, Cubelith::Vec3(2, 0, 0) },
		{ MakeStep(EAxis::Y, -1), 3, Cubelith::Vec3(0, -3, 0) },
		{ MakeStep(EAxis::Z, -1), -1, Cubelith::Vec3(0, 0, 1) },
		{ MakeStep(EAxis::Z, 1), 0, Cubelith::Vec3(0, 0, 0) },
	};

	for (const FCase& Case : Cases)
	{
		const Cubelith::FVec3 Actual = Cubelith::AxisStepVector(Case.Step, Case.Count);
		if (Actual != Case.Expected)
		{
			AddError(FString::Printf(TEXT("AxisStepVector(%s, %d) が %s（期待 %s）"),
				*Describe(Case.Step), Case.Count, *DescribeVec(Actual), *DescribeVec(Case.Expected)));
		}
	}

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
