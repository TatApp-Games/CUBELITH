#include "CubelithSeed.h"

namespace Cubelith
{
	bool TryParseSeed(const FString& Text, uint32& OutSeed)
	{
		const int32 Length = Text.Len();
		if (Length <= 0)
		{
			return false;
		}

		// 10 進の数字だけを受け付ける。FCString::Atoi64 や FString::IsNumeric は符号・空白・
		// 途中までの数字（`12a`）の扱いが目的と合わないので自前で見る
		uint64 Value = 0;
		for (int32 Index = 0; Index < Length; ++Index)
		{
			const TCHAR Character = Text[Index];
			if (Character < TEXT('0') || Character > TEXT('9'))
			{
				return false;
			}

			Value = Value * 10 + static_cast<uint64>(Character - TEXT('0'));

			// 桁を足すほど値は増えるので、ここで超えたらもう戻らない。
			// 先に弾くことで、桁数が多い文字列でも uint64 が溢れない
			if (Value > static_cast<uint64>(MaxSeedValue))
			{
				return false;
			}
		}

		OutSeed = static_cast<uint32>(Value);
		return true;
	}

	FSeedResolution ResolveSeed(
		const FString& UrlOptionText, const FString& CommandLineText, int64 PropertySeed, uint32 RandomSeed)
	{
		FSeedResolution Resolution;

		// 1. マップ URL のオプション `?seed=`
		if (!UrlOptionText.IsEmpty())
		{
			uint32 Parsed = 0;
			if (TryParseSeed(UrlOptionText, Parsed))
			{
				Resolution.Seed = Parsed;
				Resolution.Source = ESeedSource::UrlOption;
				return Resolution;
			}
			Resolution.IgnoredInputs.Add(FString::Printf(TEXT("?seed=%s"), *UrlOptionText));
		}

		// 2. コマンドライン引数 `-CubelithSeed=`
		if (!CommandLineText.IsEmpty())
		{
			uint32 Parsed = 0;
			if (TryParseSeed(CommandLineText, Parsed))
			{
				Resolution.Seed = Parsed;
				Resolution.Source = ESeedSource::CommandLine;
				return Resolution;
			}
			Resolution.IgnoredInputs.Add(FString::Printf(TEXT("-CubelithSeed=%s"), *CommandLineText));
		}

		// 3. ACubelithGameMode の UPROPERTY Seed（負の値は未指定）
		if (PropertySeed >= MinSeedValue)
		{
			if (PropertySeed <= MaxSeedValue)
			{
				Resolution.Seed = static_cast<uint32>(PropertySeed);
				Resolution.Source = ESeedSource::Property;
				return Resolution;
			}
			Resolution.IgnoredInputs.Add(FString::Printf(TEXT("Seed=%lld"), PropertySeed));
		}

		// 4. どれも無ければランダム
		Resolution.Seed = RandomSeed;
		Resolution.Source = ESeedSource::Random;
		return Resolution;
	}

	const TCHAR* SeedSourceToText(ESeedSource Source)
	{
		switch (Source)
		{
		case ESeedSource::UrlOption:
			return TEXT("URL オプション");
		case ESeedSource::CommandLine:
			return TEXT("コマンドライン");
		case ESeedSource::Property:
			return TEXT("プロパティ");
		case ESeedSource::Random:
			return TEXT("ランダム");
		}
		return TEXT("不明");
	}
}
