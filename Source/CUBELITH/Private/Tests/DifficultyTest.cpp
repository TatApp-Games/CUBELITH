// CubelithDifficulty（難易度 N / M / パズルの回転の外部指定の解釈。Docs/SPEC_UE.md 7.7）のテスト。
// 確かめるのは受け付ける書き方（整数・真偽）・読めない指定を無視して次の順位へ落ちること・項目ごとの優先順位・
// 範囲外の丸めとプリセットへの寄せ・RULES.md 3.1 の M のプリセット表との一致。
// AGameModeBase を実際に起動するテストは書かない（ワールドが要る）ので、純粋関数の範囲で見る（SeedTest と同じ）。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithDifficulty.h"
#include "Generate.h"

namespace CubelithRenderTests
{
	namespace
	{
		using Cubelith::EDifficultySource;

		FString Describe(EDifficultySource Source)
		{
			return FString(Cubelith::DifficultySourceToText(Source));
		}

		/** ResolveDifficulty の入力（9 個）と期待。プロパティは ACubelithGameMode の既定値 3 / 4 / なし を基準にする */
		struct FResolveCase
		{
			const TCHAR* Description;
			const TCHAR* UrlN;
			const TCHAR* UrlM;
			const TCHAR* UrlRot;
			const TCHAR* CliN;
			const TCHAR* CliM;
			const TCHAR* CliRot;
			int32 PropN;
			int32 PropM;
			bool bPropRot;
			int32 ExpectedN;
			int32 ExpectedM;
			bool bExpectedRot;
			EDifficultySource ExpectedNSource;
			EDifficultySource ExpectedMSource;
			EDifficultySource ExpectedRotSource;
			int32 ExpectedIgnoredCount;
			int32 ExpectedAdjustedCount;
		};

		/** 1 件分の期待をすべて見る。ずれた項目だけを AddError で報告する */
		void CheckResolveCase(FAutomationTestBase& Test, const FResolveCase& Case)
		{
			const Cubelith::FDifficultyResolution Resolution = Cubelith::ResolveDifficulty(
				Case.UrlN, Case.UrlM, Case.UrlRot, Case.CliN, Case.CliM, Case.CliRot,
				Case.PropN, Case.PropM, Case.bPropRot);

			if (Resolution.SpaceSize != Case.ExpectedN)
			{
				Test.AddError(FString::Printf(TEXT("%s: N が %d（期待 %d）"),
					Case.Description, Resolution.SpaceSize, Case.ExpectedN));
			}
			if (Resolution.PieceCount != Case.ExpectedM)
			{
				Test.AddError(FString::Printf(TEXT("%s: M が %d（期待 %d）"),
					Case.Description, Resolution.PieceCount, Case.ExpectedM));
			}
			if (Resolution.bAllowRotation != Case.bExpectedRot)
			{
				Test.AddError(FString::Printf(TEXT("%s: パズルの回転が %s（期待 %s）"),
					Case.Description, Resolution.bAllowRotation ? TEXT("あり") : TEXT("なし"),
					Case.bExpectedRot ? TEXT("あり") : TEXT("なし")));
			}
			if (Resolution.SpaceSizeSource != Case.ExpectedNSource)
			{
				Test.AddError(FString::Printf(TEXT("%s: N の経路が %s（期待 %s）"),
					Case.Description, *Describe(Resolution.SpaceSizeSource), *Describe(Case.ExpectedNSource)));
			}
			if (Resolution.PieceCountSource != Case.ExpectedMSource)
			{
				Test.AddError(FString::Printf(TEXT("%s: M の経路が %s（期待 %s）"),
					Case.Description, *Describe(Resolution.PieceCountSource), *Describe(Case.ExpectedMSource)));
			}
			if (Resolution.RotationSource != Case.ExpectedRotSource)
			{
				Test.AddError(FString::Printf(TEXT("%s: パズルの回転の経路が %s（期待 %s）"),
					Case.Description, *Describe(Resolution.RotationSource), *Describe(Case.ExpectedRotSource)));
			}
			if (Resolution.IgnoredInputs.Num() != Case.ExpectedIgnoredCount)
			{
				Test.AddError(FString::Printf(TEXT("%s: 無視した指定が %d 件（期待 %d 件）: %s"),
					Case.Description, Resolution.IgnoredInputs.Num(), Case.ExpectedIgnoredCount,
					*FString::Join(Resolution.IgnoredInputs, TEXT("、"))));
			}
			if (Resolution.AdjustedInputs.Num() != Case.ExpectedAdjustedCount)
			{
				Test.AddError(FString::Printf(TEXT("%s: 寄せた指定が %d 件（期待 %d 件）: %s"),
					Case.Description, Resolution.AdjustedInputs.Num(), Case.ExpectedAdjustedCount,
					*FString::Join(Resolution.AdjustedInputs, TEXT("、"))));
			}

			// 決まった値は必ず生成に渡せる形（GeneratePuzzle の checkf を通る範囲）になっていること
			if (Resolution.SpaceSize < Cubelith::MinSpaceSize || Resolution.SpaceSize > Cubelith::MaxSpaceSize)
			{
				Test.AddError(FString::Printf(TEXT("%s: N=%d が %d..%d の外"),
					Case.Description, Resolution.SpaceSize, Cubelith::MinSpaceSize, Cubelith::MaxSpaceSize));
				return;
			}
			if (!Cubelith::PiecePresets(Resolution.SpaceSize).Contains(Resolution.PieceCount))
			{
				Test.AddError(FString::Printf(TEXT("%s: M=%d が N=%d のプリセットに無い"),
					Case.Description, Resolution.PieceCount, Resolution.SpaceSize));
			}
		}
	}
}

// 1. TryParseDifficultyInt が受け付ける値: 符号付きでもよい 10 進整数
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithDifficultyParseIntValidTest, "CUBELITH.Render.Difficulty.ParseIntValid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithDifficultyParseIntValidTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Text;
		int64 Expected;
	};

	const FCase Cases[] = {
		{ TEXT("0"), 0 },
		{ TEXT("4"), 4 },
		{ TEXT("27"), 27 },
		{ TEXT("+4"), 4 },                     // 先頭の符号は受け付ける（Web 版の INTEGER_PATTERN と同じ）
		{ TEXT("-1"), -1 },                    // 負でも読めて、丸めは ResolveDifficulty が行う
		{ TEXT("-0"), 0 },
		{ TEXT("007"), 7 },                    // 先頭の 0 は詰めるだけ
		{ TEXT("9007199254740991"), 9007199254740991 },   // JavaScript の安全な整数の上限
		{ TEXT("-9007199254740991"), -9007199254740991 },
	};

	for (const FCase& Case : Cases)
	{
		int64 Actual = MIN_int64;
		if (!Cubelith::TryParseDifficultyInt(Case.Text, Actual))
		{
			AddError(FString::Printf(TEXT("TryParseDifficultyInt(\"%s\") が false を返した（期待 %lld）"),
				Case.Text, Case.Expected));
			continue;
		}
		if (Actual != Case.Expected)
		{
			AddError(FString::Printf(TEXT("TryParseDifficultyInt(\"%s\") が %lld（期待 %lld）"),
				Case.Text, Actual, Case.Expected));
		}
	}

	return !HasAnyErrors();
}

// 2. TryParseDifficultyInt が弾く値: 空・符号だけ・途中まで数字・空白・安全な整数を超える桁
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithDifficultyParseIntInvalidTest, "CUBELITH.Render.Difficulty.ParseIntInvalid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithDifficultyParseIntInvalidTest::RunTest(const FString& Parameters)
{
	const TCHAR* Cases[] = {
		TEXT(""),                       // 空文字（= 未指定と同じ扱い）
		TEXT("+"),                      // 符号だけ
		TEXT("-"),
		TEXT("--1"),
		TEXT("1-"),
		TEXT("12a"),                    // 途中まで数字
		TEXT("a12"),
		TEXT(" 12"),                    // 前後の空白は受け付けない（CubelithDifficulty.h の「解釈」）
		TEXT("12 "),
		TEXT("1.5"),
		TEXT("1,000"),
		TEXT("0x10"),                   // 16 進は受け付けない
		TEXT("１２"),                   // 全角数字は受け付けない
		TEXT("9007199254740992"),       // 安全な整数 + 1
		TEXT("99999999999999999999"),   // int64 も溢れる桁数
	};

	for (const TCHAR* Text : Cases)
	{
		int64 Actual = MIN_int64;
		if (Cubelith::TryParseDifficultyInt(Text, Actual))
		{
			AddError(FString::Printf(TEXT("TryParseDifficultyInt(\"%s\") が通ってしまった: %lld"), Text, Actual));
		}
	}

	return !HasAnyErrors();
}

// 3. TryParseDifficultyBool が受け付ける語（Web 版 params.ts と同じ 8 語。大文字小文字は問わない）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithDifficultyParseBoolTest, "CUBELITH.Render.Difficulty.ParseBool",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithDifficultyParseBoolTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Text;
		bool bExpected;
	};

	const FCase Cases[] = {
		{ TEXT("1"), true },
		{ TEXT("true"), true },
		{ TEXT("on"), true },
		{ TEXT("yes"), true },
		{ TEXT("TRUE"), true },     // 大文字小文字は問わない
		{ TEXT("On"), true },
		{ TEXT("YeS"), true },
		{ TEXT("0"), false },
		{ TEXT("false"), false },
		{ TEXT("off"), false },
		{ TEXT("no"), false },
		{ TEXT("FALSE"), false },
		{ TEXT("Off"), false },
	};

	for (const FCase& Case : Cases)
	{
		bool bActual = !Case.bExpected;
		if (!Cubelith::TryParseDifficultyBool(Case.Text, bActual))
		{
			AddError(FString::Printf(TEXT("TryParseDifficultyBool(\"%s\") が false を返した"), Case.Text));
			continue;
		}
		if (bActual != Case.bExpected)
		{
			AddError(FString::Printf(TEXT("TryParseDifficultyBool(\"%s\") が %s（期待 %s）"),
				Case.Text, bActual ? TEXT("true") : TEXT("false"), Case.bExpected ? TEXT("true") : TEXT("false")));
		}
	}

	const TCHAR* InvalidCases[] = {
		TEXT(""),       // 空文字（= 未指定と同じ扱い）
		TEXT("2"),
		TEXT("-1"),
		TEXT("t"),      // 略した語は受け付けない
		TEXT("y"),
		TEXT("maybe"),
		TEXT("あり"),
		TEXT(" 1"),     // 前後の空白は受け付けない
		TEXT("1 "),
	};

	for (const TCHAR* Text : InvalidCases)
	{
		bool bActual = false;
		if (Cubelith::TryParseDifficultyBool(Text, bActual))
		{
			AddError(FString::Printf(TEXT("TryParseDifficultyBool(\"%s\") が通ってしまった: %s"),
				Text, bActual ? TEXT("true") : TEXT("false")));
		}
	}

	return !HasAnyErrors();
}

// 4. PiecePresets が RULES.md 3.1 の表（N = 3..7 の 5 段）と一致する
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithDifficultyPresetsTest, "CUBELITH.Render.Difficulty.Presets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithDifficultyPresetsTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		int32 N;
		TArray<int32> Expected;
	};

	// RULES.md 3.1 の表をそのまま書き写したもの（Web 版 difficulty.ts の piecePresets と同じ）
	const FCase Cases[] = {
		{ 3, { 3, 4, 5, 6, 7 } },
		{ 4, { 4, 6, 8, 10, 12 } },
		{ 5, { 5, 8, 11, 14, 17 } },
		{ 6, { 6, 10, 14, 18, 22 } },
		{ 7, { 7, 12, 17, 22, 27 } },
	};

	for (const FCase& Case : Cases)
	{
		const TArray<int32> Actual = Cubelith::PiecePresets(Case.N);
		if (Actual != Case.Expected)
		{
			AddError(FString::Printf(TEXT("N=%d のプリセットが %s（期待 %s）"), Case.N,
				*FString::JoinBy(Actual, TEXT(" / "), [](int32 V) { return FString::FromInt(V); }),
				*FString::JoinBy(Case.Expected, TEXT(" / "), [](int32 V) { return FString::FromInt(V); })));
			continue;
		}

		// 表の最大は MaxPieces(N)（= N + 4(N−2)）と一致し、刻みは N−2
		if (Actual.Last() != Cubelith::MaxPieces(Case.N))
		{
			AddError(FString::Printf(TEXT("N=%d の最大が %d（MaxPieces は %d）"),
				Case.N, Actual.Last(), Cubelith::MaxPieces(Case.N)));
		}
		for (int32 Index = 1; Index < Actual.Num(); ++Index)
		{
			if (Actual[Index] - Actual[Index - 1] != Case.N - 2)
			{
				AddError(FString::Printf(TEXT("N=%d の刻みが %d（期待 %d）"),
					Case.N, Actual[Index] - Actual[Index - 1], Case.N - 2));
			}
		}
	}

	return !HasAnyErrors();
}

// 5. NearestPreset: 最も近い段へ寄せ、同じ距離なら小さい方
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithDifficultyNearestPresetTest, "CUBELITH.Render.Difficulty.NearestPreset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithDifficultyNearestPresetTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		int32 N;
		int32 M;
		int32 Expected;
	};

	const FCase Cases[] = {
		{ 3, 4, 4 },    // 段そのままなら動かない
		{ 3, 7, 7 },
		{ 4, 8, 8 },
		{ 4, 7, 6 },    // 6 と 8 から等距離 → 小さい方
		{ 4, 9, 8 },
		{ 4, 5, 4 },    // 4 と 6 から等距離 → 小さい方
		{ 4, 2, 4 },    // 段より小さければ最小の段
		{ 4, 99, 12 },  // 段より大きければ最大の段
		{ 5, 10, 11 },  // 8 と 11 なら 11 のほうが近い
		{ 5, 9, 8 },
		{ 7, 4, 7 },
		{ 7, 15, 17 },  // 12 と 17 なら 17 のほうが近い
		{ 7, 14, 12 },  // 12 と 17 なら 12 のほうが近い
	};

	for (const FCase& Case : Cases)
	{
		const int32 Actual = Cubelith::NearestPreset(Cubelith::PiecePresets(Case.N), Case.M);
		if (Actual != Case.Expected)
		{
			AddError(FString::Printf(TEXT("N=%d で M=%d を寄せた結果が %d（期待 %d）"),
				Case.N, Case.M, Actual, Case.Expected));
		}
	}

	// 段が空なら最小のピース数を返す（呼ばれない経路だが、落ちないことを確かめる）
	if (Cubelith::NearestPreset(TArray<int32>(), 5) != Cubelith::MinPieceCount)
	{
		AddError(TEXT("空のプリセットで MinPieceCount が返らなかった"));
	}

	return !HasAnyErrors();
}

// 6. ResolveDifficulty の優先順位: URL オプション > コマンドライン > プロパティ。項目ごとに独立して決まる
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithDifficultyPriorityTest, "CUBELITH.Render.Difficulty.Priority",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithDifficultyPriorityTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;
	using Cubelith::EDifficultySource;

	const FResolveCase Cases[] = {
		// 全部そろっていれば URL オプションが勝つ
		{ TEXT("全部指定"), TEXT("5"), TEXT("8"), TEXT("1"), TEXT("6"), TEXT("10"), TEXT("0"), 7, 27, true,
			5, 8, true,
			EDifficultySource::UrlOption, EDifficultySource::UrlOption, EDifficultySource::UrlOption, 0, 0 },
		// URL が無ければコマンドライン
		{ TEXT("URL なし"), TEXT(""), TEXT(""), TEXT(""), TEXT("6"), TEXT("10"), TEXT("on"), 7, 27, false,
			6, 10, true,
			EDifficultySource::CommandLine, EDifficultySource::CommandLine, EDifficultySource::CommandLine, 0, 0 },
		// どちらも無ければプロパティ
		{ TEXT("プロパティだけ"), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), 4, 6, true,
			4, 6, true,
			EDifficultySource::Property, EDifficultySource::Property, EDifficultySource::Property, 0, 0 },
		// 既定（ACubelithGameMode の初期値）はそのまま通る
		{ TEXT("既定"), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), 3, 4, false,
			3, 4, false,
			EDifficultySource::Property, EDifficultySource::Property, EDifficultySource::Property, 0, 0 },
		// 項目ごとに独立: `?n=` だけ指定したら M と回転はプロパティの値
		{ TEXT("N だけ URL"), TEXT("4"), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), 3, 4, false,
			4, 4, false,
			EDifficultySource::UrlOption, EDifficultySource::Property, EDifficultySource::Property, 0, 0 },
		// 項目ごとに独立: N はコマンドライン・M は URL・回転はプロパティ
		{ TEXT("項目ごとに別の経路"), TEXT(""), TEXT("12"), TEXT(""), TEXT("7"), TEXT(""), TEXT(""), 3, 4, true,
			7, 12, true,
			EDifficultySource::CommandLine, EDifficultySource::UrlOption, EDifficultySource::Property, 0, 0 },
		// 回転だけ指定（偽の語）。プロパティが「あり」でも URL の「なし」が勝つ
		{ TEXT("回転だけ URL で偽"), TEXT(""), TEXT(""), TEXT("no"), TEXT(""), TEXT(""), TEXT("1"), 3, 4, true,
			3, 4, false,
			EDifficultySource::Property, EDifficultySource::Property, EDifficultySource::UrlOption, 0, 0 },
		// 回転はコマンドラインで偽、プロパティは真
		{ TEXT("回転はコマンドラインで偽"), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("off"), 3, 4, true,
			3, 4, false,
			EDifficultySource::Property, EDifficultySource::Property, EDifficultySource::CommandLine, 0, 0 },
	};

	for (const FResolveCase& Case : Cases)
	{
		CheckResolveCase(*this, Case);
	}

	return !HasAnyErrors();
}

// 7. ResolveDifficulty のフォールバック: 読めない指定は無視して次の順位へ落ち、無視したことが結果に残る
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithDifficultyFallbackTest, "CUBELITH.Render.Difficulty.Fallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithDifficultyFallbackTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;
	using Cubelith::EDifficultySource;

	const FResolveCase Cases[] = {
		// URL が読めなければコマンドラインへ落ちる
		{ TEXT("N の URL が不正"), TEXT("12a"), TEXT(""), TEXT(""), TEXT("5"), TEXT(""), TEXT(""), 3, 5, false,
			5, 5, false,
			EDifficultySource::CommandLine, EDifficultySource::Property, EDifficultySource::Property, 1, 0 },
		// URL もコマンドラインも読めなければプロパティへ
		{ TEXT("N が両方不正"), TEXT("1.5"), TEXT(""), TEXT(""), TEXT("x"), TEXT(""), TEXT(""), 6, 6, false,
			6, 6, false,
			EDifficultySource::Property, EDifficultySource::Property, EDifficultySource::Property, 2, 0 },
		// 桁が多すぎる値も「読めない」扱い（丸めではなく次の順位へ）
		{ TEXT("N が安全な整数を超える"), TEXT("99999999999999999999"), TEXT(""), TEXT(""),
			TEXT(""), TEXT(""), TEXT(""), 4, 4, false,
			4, 4, false,
			EDifficultySource::Property, EDifficultySource::Property, EDifficultySource::Property, 1, 0 },
		// 回転の語が不正ならコマンドラインへ落ちる
		{ TEXT("回転の URL が不正"), TEXT(""), TEXT(""), TEXT("maybe"), TEXT(""), TEXT(""), TEXT("yes"), 3, 4, false,
			3, 4, true,
			EDifficultySource::Property, EDifficultySource::Property, EDifficultySource::CommandLine, 1, 0 },
		// 回転が両方不正ならプロパティへ
		{ TEXT("回転が両方不正"), TEXT(""), TEXT(""), TEXT("2"), TEXT(""), TEXT(""), TEXT("t"), 3, 4, true,
			3, 4, true,
			EDifficultySource::Property, EDifficultySource::Property, EDifficultySource::Property, 2, 0 },
		// 項目ごとに独立しているので、M が不正でも N は URL のまま
		{ TEXT("M だけ不正"), TEXT("4"), TEXT("x"), TEXT(""), TEXT(""), TEXT("6"), TEXT(""), 3, 4, false,
			4, 6, false,
			EDifficultySource::UrlOption, EDifficultySource::CommandLine, EDifficultySource::Property, 1, 0 },
		// 全部不正ならすべてプロパティ（真偽には「未指定」が無いのでプロパティが最後の受け皿）
		{ TEXT("全部不正"), TEXT("a"), TEXT("b"), TEXT("c"), TEXT("d"), TEXT("e"), TEXT("f"), 5, 11, true,
			5, 11, true,
			EDifficultySource::Property, EDifficultySource::Property, EDifficultySource::Property, 6, 0 },
	};

	for (const FResolveCase& Case : Cases)
	{
		CheckResolveCase(*this, Case);
	}

	// 無視した指定は、そのままログに出せる形（打ち間違えた値が分かる形）で N → M → 回転の順に残る
	const Cubelith::FDifficultyResolution Resolution = Cubelith::ResolveDifficulty(
		TEXT("12a"), TEXT(""), TEXT("maybe"), TEXT(""), TEXT("1.5"), TEXT(""), 3, 4, false);
	if (Resolution.IgnoredInputs.Num() == 3)
	{
		TestEqual(TEXT("無視した ?n="), Resolution.IgnoredInputs[0], FString(TEXT("?n=12a")));
		TestEqual(TEXT("無視した -CubelithM="), Resolution.IgnoredInputs[1], FString(TEXT("-CubelithM=1.5")));
		TestEqual(TEXT("無視した ?rot="), Resolution.IgnoredInputs[2], FString(TEXT("?rot=maybe")));
	}
	else
	{
		AddError(FString::Printf(TEXT("無視した指定が %d 件（期待 3 件）"), Resolution.IgnoredInputs.Num()));
	}

	return !HasAnyErrors();
}

// 8. ResolveDifficulty の丸め: N は 3..7 に丸め、M は N のプリセットへ寄せる（シードと違って捨てない）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithDifficultyClampTest, "CUBELITH.Render.Difficulty.Clamp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithDifficultyClampTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;
	using Cubelith::EDifficultySource;

	const FResolveCase Cases[] = {
		// N が上限超え → 7 に丸める。M（プロパティの 4）は N=7 のプリセットへ寄るので寄せは 2 件
		{ TEXT("N が上限超え"), TEXT("9"), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), 3, 4, false,
			7, 7, false,
			EDifficultySource::UrlOption, EDifficultySource::Property, EDifficultySource::Property, 0, 2 },
		// N が負 → 3 に丸める（Web 版の clampInt と同じ。捨てて次の順位へ落ちるのではない）
		{ TEXT("N が負"), TEXT("-1"), TEXT(""), TEXT(""), TEXT("5"), TEXT(""), TEXT(""), 3, 4, false,
			3, 4, false,
			EDifficultySource::UrlOption, EDifficultySource::Property, EDifficultySource::Property, 0, 1 },
		// N が 0 → 3 に丸める
		{ TEXT("N が 0"), TEXT("0"), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), 3, 4, false,
			3, 4, false,
			EDifficultySource::UrlOption, EDifficultySource::Property, EDifficultySource::Property, 0, 1 },
		// N が安全な整数の上限 → 7 に丸める（読めるので無視ではない）
		{ TEXT("N が安全な整数の上限"), TEXT("9007199254740991"), TEXT("27"), TEXT(""),
			TEXT(""), TEXT(""), TEXT(""), 3, 4, false,
			7, 27, false,
			EDifficultySource::UrlOption, EDifficultySource::UrlOption, EDifficultySource::Property, 0, 1 },
		// M が段の間 → 等距離なので小さい段へ（N=4 の 6 と 8）
		{ TEXT("M が段の間"), TEXT("4"), TEXT("7"), TEXT(""), TEXT(""), TEXT(""), TEXT(""), 3, 4, false,
			4, 6, false,
			EDifficultySource::UrlOption, EDifficultySource::UrlOption, EDifficultySource::Property, 0, 1 },
		// M が上限超え → 最大の段
		{ TEXT("M が上限超え"), TEXT("4"), TEXT("100"), TEXT(""), TEXT(""), TEXT(""), TEXT(""), 3, 4, false,
			4, 12, false,
			EDifficultySource::UrlOption, EDifficultySource::UrlOption, EDifficultySource::Property, 0, 1 },
		// M が 0 → 最小の段（N=3 なら 3）
		{ TEXT("M が 0"), TEXT(""), TEXT("0"), TEXT(""), TEXT(""), TEXT(""), TEXT(""), 3, 4, false,
			3, 3, false,
			EDifficultySource::Property, EDifficultySource::UrlOption, EDifficultySource::Property, 0, 1 },
		// プロパティが範囲外でも同じように丸める（人がエディタで動かしたとき）
		{ TEXT("プロパティが範囲外"), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), 99, 99, false,
			7, 27, false,
			EDifficultySource::Property, EDifficultySource::Property, EDifficultySource::Property, 0, 2 },
	};

	for (const FResolveCase& Case : Cases)
	{
		CheckResolveCase(*this, Case);
	}

	// 寄せた指定は「何をどう寄せたか」が分かる形で残る
	const Cubelith::FDifficultyResolution Resolution =
		Cubelith::ResolveDifficulty(TEXT("9"), TEXT("100"), TEXT(""), TEXT(""), TEXT(""), TEXT(""), 3, 4, false);
	if (Resolution.AdjustedInputs.Num() == 2)
	{
		TestEqual(TEXT("寄せた ?n="), Resolution.AdjustedInputs[0], FString(TEXT("?n=9 → 7")));
		TestEqual(TEXT("寄せた ?m="), Resolution.AdjustedInputs[1], FString(TEXT("?m=100 → 27")));
	}
	else
	{
		AddError(FString::Printf(TEXT("寄せた指定が %d 件（期待 2 件）: %s"),
			Resolution.AdjustedInputs.Num(), *FString::Join(Resolution.AdjustedInputs, TEXT("、"))));
	}

	// N を切り替えても M は必ずその N のプリセットに収まる（生成の checkf に落ちない）
	for (int32 N = Cubelith::MinSpaceSize; N <= Cubelith::MaxSpaceSize; ++N)
	{
		for (int32 M = -3; M <= 40; ++M)
		{
			const Cubelith::FDifficultyResolution Each = Cubelith::ResolveDifficulty(
				FString::FromInt(N), FString::FromInt(M), TEXT(""), TEXT(""), TEXT(""), TEXT(""), 3, 4, false);
			if (Each.SpaceSize != N)
			{
				AddError(FString::Printf(TEXT("N=%d が %d になった"), N, Each.SpaceSize));
				continue;
			}
			if (!Cubelith::PiecePresets(N).Contains(Each.PieceCount))
			{
				AddError(FString::Printf(TEXT("N=%d, M=%d → %d が プリセットに無い"), N, M, Each.PieceCount));
			}
		}
	}

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
