// CubelithSeed（乱数シードの外部指定の解釈。Docs/SPEC_UE.md 7.7）のテスト。
// 確かめるのは受け付ける値の範囲・不正値を弾くこと・優先順位（URL > コマンドライン > プロパティ > ランダム）と
// 不正値が次の順位へ落ちること。AGameModeBase を実際に起動するテストは書かない（ワールドが要る）ので、
// 純粋関数の範囲で見る。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CubelithSeed.h"

namespace CubelithRenderTests
{
	namespace
	{
		/** ランダムの控えに使う値。これが返ったら「どの指定も採られなかった」ことが分かるよう目立つ値にする */
		constexpr uint32 FallbackSeed = 123456789u;

		FString Describe(Cubelith::ESeedSource Source)
		{
			return FString(Cubelith::SeedSourceToText(Source));
		}
	}
}

// 1. TryParseSeed が受け付ける値: 0..4294967295 の 10 進整数
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSeedParseValidTest, "CUBELITH.Render.Seed.ParseValid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSeedParseValidTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	struct FCase
	{
		const TCHAR* Text;
		uint32 Expected;
	};

	const FCase Cases[] = {
		{ TEXT("0"), 0u },
		{ TEXT("1"), 1u },
		{ TEXT("123"), 123u },
		{ TEXT("2147483648"), 2147483648u },        // int32 に収まらない値も通ること
		{ TEXT("4294967295"), 4294967295u },        // 上限
		{ TEXT("007"), 7u },                        // 先頭の 0 は詰めるだけ（桁数は制限しない）
		{ TEXT("0000000000000000000000012"), 12u }, // 桁が多くても uint64 が溢れないこと
	};

	for (const FCase& Case : Cases)
	{
		uint32 Actual = FallbackSeed;
		if (!Cubelith::TryParseSeed(Case.Text, Actual))
		{
			AddError(FString::Printf(TEXT("TryParseSeed(\"%s\") が false を返した（期待 %u）"),
				Case.Text, Case.Expected));
			continue;
		}
		if (Actual != Case.Expected)
		{
			AddError(FString::Printf(TEXT("TryParseSeed(\"%s\") が %u（期待 %u）"),
				Case.Text, Actual, Case.Expected));
		}
	}

	return !HasAnyErrors();
}

// 2. TryParseSeed が弾く値: 範囲外・数字以外・空・空白や符号付き
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSeedParseInvalidTest, "CUBELITH.Render.Seed.ParseInvalid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSeedParseInvalidTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;

	const TCHAR* Cases[] = {
		TEXT(""),                       // 空文字（= 未指定と同じ扱い）
		TEXT("4294967296"),             // 上限 + 1
		TEXT("99999999999999999999"),   // uint64 も溢れる桁数
		TEXT("-1"),                     // 負の符号は受け付けない
		TEXT("+1"),                     // 正の符号も受け付けない
		TEXT("12a"),                    // 途中まで数字
		TEXT("a12"),
		TEXT(" 12"),                    // 前後の空白は受け付けない（CubelithSeed.h の「解釈」）
		TEXT("12 "),
		TEXT("1.5"),
		TEXT("1,000"),
		TEXT("0x10"),                   // 16 進は受け付けない
		TEXT("１２３"),                 // 全角数字は受け付けない
	};

	for (const TCHAR* Text : Cases)
	{
		uint32 Actual = FallbackSeed;
		if (Cubelith::TryParseSeed(Text, Actual))
		{
			AddError(FString::Printf(TEXT("TryParseSeed(\"%s\") が通ってしまった: %u"), Text, Actual));
		}
	}

	return !HasAnyErrors();
}

// 3. ResolveSeed の優先順位: URL オプション > コマンドライン > プロパティ > ランダム
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSeedPriorityTest, "CUBELITH.Render.Seed.Priority",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSeedPriorityTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;
	using Cubelith::ESeedSource;

	struct FCase
	{
		const TCHAR* Description;
		const TCHAR* UrlOption;
		const TCHAR* CommandLine;
		int64 Property;
		uint32 ExpectedSeed;
		ESeedSource ExpectedSource;
	};

	const FCase Cases[] = {
		// 全部そろっていれば URL オプションが勝つ
		{ TEXT("全部指定"), TEXT("1"), TEXT("2"), 3, 1u, ESeedSource::UrlOption },
		// URL が無ければコマンドライン
		{ TEXT("URL なし"), TEXT(""), TEXT("2"), 3, 2u, ESeedSource::CommandLine },
		// URL もコマンドラインも無ければプロパティ
		{ TEXT("プロパティだけ"), TEXT(""), TEXT(""), 3, 3u, ESeedSource::Property },
		// どれも無ければランダムの控え
		{ TEXT("指定なし"), TEXT(""), TEXT(""), -1, FallbackSeed, ESeedSource::Random },
		// プロパティの 0 は「未指定」ではなく有効な値
		{ TEXT("プロパティ 0"), TEXT(""), TEXT(""), 0, 0u, ESeedSource::Property },
		// プロパティの上限
		{ TEXT("プロパティ上限"), TEXT(""), TEXT(""), 4294967295, 4294967295u, ESeedSource::Property },
		// URL の 0 も有効な値（空文字と混同しない）
		{ TEXT("URL 0"), TEXT("0"), TEXT("2"), 3, 0u, ESeedSource::UrlOption },
	};

	for (const FCase& Case : Cases)
	{
		const Cubelith::FSeedResolution Resolution =
			Cubelith::ResolveSeed(Case.UrlOption, Case.CommandLine, Case.Property, FallbackSeed);

		if (Resolution.Seed != Case.ExpectedSeed)
		{
			AddError(FString::Printf(TEXT("%s: シードが %u（期待 %u）"),
				Case.Description, Resolution.Seed, Case.ExpectedSeed));
		}
		if (Resolution.Source != Case.ExpectedSource)
		{
			AddError(FString::Printf(TEXT("%s: 経路が %s（期待 %s）"),
				Case.Description, *Describe(Resolution.Source), *Describe(Case.ExpectedSource)));
		}
		if (Resolution.IgnoredInputs.Num() != 0)
		{
			AddError(FString::Printf(TEXT("%s: 無視した指定が %d 件あった（期待 0 件）"),
				Case.Description, Resolution.IgnoredInputs.Num()));
		}
	}

	return !HasAnyErrors();
}

// 4. ResolveSeed のフォールバック: 不正な指定は無視して次の順位へ落ち、無視したことが結果に残る
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSeedFallbackTest, "CUBELITH.Render.Seed.Fallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSeedFallbackTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests;
	using Cubelith::ESeedSource;

	struct FCase
	{
		const TCHAR* Description;
		const TCHAR* UrlOption;
		const TCHAR* CommandLine;
		int64 Property;
		uint32 ExpectedSeed;
		ESeedSource ExpectedSource;
		int32 ExpectedIgnoredCount;
	};

	const FCase Cases[] = {
		// URL が不正ならコマンドラインへ落ちる
		{ TEXT("URL が不正"), TEXT("12a"), TEXT("2"), 3, 2u, ESeedSource::CommandLine, 1 },
		// URL の範囲外も同じ
		{ TEXT("URL が範囲外"), TEXT("4294967296"), TEXT("2"), 3, 2u, ESeedSource::CommandLine, 1 },
		// URL もコマンドラインも不正ならプロパティへ
		{ TEXT("URL とコマンドラインが不正"), TEXT("-1"), TEXT("x"), 3, 3u, ESeedSource::Property, 2 },
		// 全部不正ならランダムへ。プロパティの範囲外も無視した指定として数える
		{ TEXT("全部不正"), TEXT("-1"), TEXT("x"), 4294967296, FallbackSeed, ESeedSource::Random, 3 },
		// プロパティだけ範囲外
		{ TEXT("プロパティが範囲外"), TEXT(""), TEXT(""), 4294967296, FallbackSeed, ESeedSource::Random, 1 },
		// 負のプロパティは「未指定」なので無視した指定には数えない
		{ TEXT("プロパティが負"), TEXT(""), TEXT(""), -5, FallbackSeed, ESeedSource::Random, 0 },
		// 不正でも後ろの順位が使われれば、そこで止まる（プロパティは見に行かない）
		{ TEXT("不正な URL の後ろで決まる"), TEXT("bad"), TEXT("7"), 4294967296, 7u, ESeedSource::CommandLine, 1 },
	};

	for (const FCase& Case : Cases)
	{
		const Cubelith::FSeedResolution Resolution =
			Cubelith::ResolveSeed(Case.UrlOption, Case.CommandLine, Case.Property, FallbackSeed);

		if (Resolution.Seed != Case.ExpectedSeed)
		{
			AddError(FString::Printf(TEXT("%s: シードが %u（期待 %u）"),
				Case.Description, Resolution.Seed, Case.ExpectedSeed));
		}
		if (Resolution.Source != Case.ExpectedSource)
		{
			AddError(FString::Printf(TEXT("%s: 経路が %s（期待 %s）"),
				Case.Description, *Describe(Resolution.Source), *Describe(Case.ExpectedSource)));
		}
		if (Resolution.IgnoredInputs.Num() != Case.ExpectedIgnoredCount)
		{
			AddError(FString::Printf(TEXT("%s: 無視した指定が %d 件（期待 %d 件）: %s"),
				Case.Description, Resolution.IgnoredInputs.Num(), Case.ExpectedIgnoredCount,
				*FString::Join(Resolution.IgnoredInputs, TEXT("、"))));
		}
	}

	// 無視した指定は、そのままログに出せる形（打ち間違えた値が分かる形）で残る
	const Cubelith::FSeedResolution Resolution =
		Cubelith::ResolveSeed(TEXT("12a"), TEXT("x"), 4294967296, FallbackSeed);
	if (Resolution.IgnoredInputs.Num() == 3)
	{
		TestEqual(TEXT("無視した URL オプション"), Resolution.IgnoredInputs[0], FString(TEXT("?seed=12a")));
		TestEqual(TEXT("無視したコマンドライン"), Resolution.IgnoredInputs[1], FString(TEXT("-CubelithSeed=x")));
		TestEqual(TEXT("無視したプロパティ"), Resolution.IgnoredInputs[2], FString(TEXT("Seed=4294967296")));
	}

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
