#include "CubelithDifficulty.h"

#include "Generate.h"

namespace Cubelith
{
	namespace
	{
		/**
		 * 整数として受け付ける絶対値の上限。JavaScript の Number.MAX_SAFE_INTEGER（2^53 − 1）に合わせてある。
		 * Web 版（params.ts の parseIntParam）はここを超える値を Number.isSafeInteger で弾いて「指定なし」に
		 * するので、UE 版も同じところで不正にする（丸めずに次の優先順位へ落ちる）
		 */
		constexpr int64 MaxSafeIntegerValue = 9007199254740991LL;

		/** 真として受け付ける語（Web 版 params.ts の TRUE_WORDS と同じ） */
		const TCHAR* TrueWords[] = { TEXT("1"), TEXT("true"), TEXT("on"), TEXT("yes") };

		/** 偽として受け付ける語（Web 版 params.ts の FALSE_WORDS と同じ） */
		const TCHAR* FalseWords[] = { TEXT("0"), TEXT("false"), TEXT("off"), TEXT("no") };

		/** 整数の項目 1 つ分の名前。無視した指定・寄せた指定をログに出せる形にするのに使う */
		struct FIntItemNames
		{
			/** マップ URL のオプション名（`n`） */
			const TCHAR* OptionName;
			/** コマンドライン引数の名前（`CubelithN`） */
			const TCHAR* CommandLineName;
			/** UPROPERTY の名前（`SpaceSize`） */
			const TCHAR* PropertyName;
		};

		const FIntItemNames SpaceSizeNames = { TEXT("n"), TEXT("CubelithN"), TEXT("SpaceSize") };
		const FIntItemNames PieceCountNames = { TEXT("m"), TEXT("CubelithM"), TEXT("PieceCount") };
		const FIntItemNames RotationNames = { TEXT("rot"), TEXT("CubelithRotation"), TEXT("bAllowRotation") };

		/** 指定を「打ち間違えた値が分かる形」にする（`?n=12a`・`-CubelithN=12a`） */
		FString DescribeInput(EDifficultySource Source, const FIntItemNames& Names, const FString& Text)
		{
			switch (Source)
			{
			case EDifficultySource::UrlOption:
				return FString::Printf(TEXT("?%s=%s"), Names.OptionName, *Text);
			case EDifficultySource::CommandLine:
				return FString::Printf(TEXT("-%s=%s"), Names.CommandLineName, *Text);
			default:
				return FString::Printf(TEXT("%s=%s"), Names.PropertyName, *Text);
			}
		}

		/**
		 * 整数の項目を 1 つ、URL オプション > コマンドライン > プロパティの順で決める（値の範囲は見ない）。
		 * 読めなかった指定は Resolution.IgnoredInputs に積んで次の順位へ落ちる（シードと同じ）。
		 * プロパティは int32 なので必ず読める = 最後の受け皿になる。
		 */
		void ResolveIntItem(const FString& OptionText, const FString& CommandLineText, int32 PropertyValue,
			const FIntItemNames& Names, FDifficultyResolution& Resolution, int64& OutRaw, EDifficultySource& OutSource)
		{
			if (!OptionText.IsEmpty())
			{
				int64 Parsed = 0;
				if (TryParseDifficultyInt(OptionText, Parsed))
				{
					OutRaw = Parsed;
					OutSource = EDifficultySource::UrlOption;
					return;
				}
				Resolution.IgnoredInputs.Add(DescribeInput(EDifficultySource::UrlOption, Names, OptionText));
			}

			if (!CommandLineText.IsEmpty())
			{
				int64 Parsed = 0;
				if (TryParseDifficultyInt(CommandLineText, Parsed))
				{
					OutRaw = Parsed;
					OutSource = EDifficultySource::CommandLine;
					return;
				}
				Resolution.IgnoredInputs.Add(DescribeInput(EDifficultySource::CommandLine, Names, CommandLineText));
			}

			OutRaw = PropertyValue;
			OutSource = EDifficultySource::Property;
		}

		/** 範囲外 / プリセット外で寄せたことを記録する（`?n=9 → 7`） */
		void NoteAdjustment(EDifficultySource Source, const FIntItemNames& Names, int64 Raw, int32 Adjusted,
			FDifficultyResolution& Resolution)
		{
			Resolution.AdjustedInputs.Add(FString::Printf(TEXT("%s → %d"),
				*DescribeInput(Source, Names, FString::Printf(TEXT("%lld"), Raw)), Adjusted));
		}
	}

	bool TryParseDifficultyInt(const FString& Text, int64& OutValue)
	{
		const int32 Length = Text.Len();
		if (Length <= 0)
		{
			return false;
		}

		// 先頭の符号は付いていてもよい（Web 版の INTEGER_PATTERN `/^[+-]?\d+$/` と同じ）。
		// 符号だけ（`+` / `-`）は数字が無いので不正
		int32 Index = 0;
		bool bNegative = false;
		if (Text[0] == TEXT('+') || Text[0] == TEXT('-'))
		{
			bNegative = Text[0] == TEXT('-');
			Index = 1;
			if (Index >= Length)
			{
				return false;
			}
		}

		// 10 進の数字だけを受け付ける。FCString::Atoi64 や FString::IsNumeric は空白や
		// 途中までの数字（`12a`）の扱いが目的と合わないので自前で見る（TryParseSeed と同じ）
		int64 Value = 0;
		for (; Index < Length; ++Index)
		{
			const TCHAR Character = Text[Index];
			if (Character < TEXT('0') || Character > TEXT('9'))
			{
				return false;
			}

			Value = Value * 10 + static_cast<int64>(Character - TEXT('0'));

			// 桁を足すほど値は増えるので、ここで超えたらもう戻らない。
			// 先に弾くことで、桁数が多い文字列でも int64 が溢れない
			if (Value > MaxSafeIntegerValue)
			{
				return false;
			}
		}

		OutValue = bNegative ? -Value : Value;
		return true;
	}

	bool TryParseDifficultyBool(const FString& Text, bool& OutValue)
	{
		if (Text.IsEmpty())
		{
			return false;
		}

		const FString Lowered = Text.ToLower();
		for (const TCHAR* Word : TrueWords)
		{
			if (Lowered == Word)
			{
				OutValue = true;
				return true;
			}
		}
		for (const TCHAR* Word : FalseWords)
		{
			if (Lowered == Word)
			{
				OutValue = false;
				return true;
			}
		}
		return false;
	}

	TArray<int32> PiecePresets(int32 N)
	{
		// MaxPieces（= 5N − 8 = N + 4(N−2)）が N の範囲を checkf で見る
		const int32 Limit = MaxPieces(N);

		TArray<int32> Presets;
		Presets.Reserve(PiecePresetSteps);
		for (int32 Step = 0; Step < PiecePresetSteps; ++Step)
		{
			// N から Limit までを 5 段に等分する（刻みは N−2 で割り切れるので、丸めは効かない）。
			// Web 版 difficulty.ts の piecePresets と同じ式にしてあるので、段の値も同じになる
			const double T = static_cast<double>(Step) / static_cast<double>(PiecePresetSteps - 1);
			const int32 Value = N + FMath::RoundToInt32(T * static_cast<double>(Limit - N));
			// N=3 は刻みが 1 なので重複しないが、式のうえで重なりうるので Web 版と同じく重複は落とす
			Presets.AddUnique(Value);
		}
		return Presets;
	}

	int32 NearestPreset(const TArray<int32>& Presets, int32 M)
	{
		if (Presets.Num() <= 0)
		{
			return MinPieceCount;
		}

		int32 Best = Presets[0];
		for (const int32 Value : Presets)
		{
			// 「真に近い」ときだけ差し替えるので、同じ距離なら先に見た（昇順なので小さい）方が残る
			if (FMath::Abs(Value - M) < FMath::Abs(Best - M))
			{
				Best = Value;
			}
		}
		return Best;
	}

	FDifficultyResolution ResolveDifficulty(
		const FString& SpaceSizeOptionText,
		const FString& PieceCountOptionText,
		const FString& RotationOptionText,
		const FString& SpaceSizeCommandLineText,
		const FString& PieceCountCommandLineText,
		const FString& RotationCommandLineText,
		int32 PropertySpaceSize,
		int32 PropertyPieceCount,
		bool bPropertyAllowRotation)
	{
		FDifficultyResolution Resolution;

		// 1. 空間サイズ N。範囲外は 3..7 に丸める（シードと違って捨てない。このファイル冒頭の「違い」）
		{
			int64 Raw = 0;
			EDifficultySource Source = EDifficultySource::Property;
			ResolveIntItem(SpaceSizeOptionText, SpaceSizeCommandLineText, PropertySpaceSize,
				SpaceSizeNames, Resolution, Raw, Source);

			const int32 Clamped = static_cast<int32>(FMath::Clamp<int64>(Raw, MinSpaceSize, MaxSpaceSize));
			if (Clamped != Raw)
			{
				NoteAdjustment(Source, SpaceSizeNames, Raw, Clamped, Resolution);
			}
			Resolution.SpaceSize = Clamped;
			Resolution.SpaceSizeSource = Source;
		}

		// 2. ピース分割数 M。N が決まらないとプリセットが決まらないので N の次に決める。
		//    RULES.md 3.1 の 5 段のプリセットのうち最も近いものへ寄せる（範囲に丸めるだけでは
		//    プリセットに無い M で生成してしまい、難易度選択（U4）で選べない盤面になるため）
		{
			int64 Raw = 0;
			EDifficultySource Source = EDifficultySource::Property;
			ResolveIntItem(PieceCountOptionText, PieceCountCommandLineText, PropertyPieceCount,
				PieceCountNames, Resolution, Raw, Source);

			// 先に有効範囲へ落としてから寄せる。プリセットは有効範囲の中に収まっているので、
			// 寄せた結果はどちらの順でも同じ。ここで落とすのは int32 の桁に収めるため
			const int32 Bounded = static_cast<int32>(
				FMath::Clamp<int64>(Raw, MinPieceCount, MaxPieces(Resolution.SpaceSize)));
			const int32 Snapped = NearestPreset(PiecePresets(Resolution.SpaceSize), Bounded);
			if (Snapped != Raw)
			{
				NoteAdjustment(Source, PieceCountNames, Raw, Snapped, Resolution);
			}
			Resolution.PieceCount = Snapped;
			Resolution.PieceCountSource = Source;
		}

		// 3. パズルの回転。真偽値には範囲が無いので、読めたかどうかだけを見る
		{
			if (!RotationOptionText.IsEmpty())
			{
				bool bParsed = false;
				if (TryParseDifficultyBool(RotationOptionText, bParsed))
				{
					Resolution.bAllowRotation = bParsed;
					Resolution.RotationSource = EDifficultySource::UrlOption;
					return Resolution;
				}
				Resolution.IgnoredInputs.Add(
					DescribeInput(EDifficultySource::UrlOption, RotationNames, RotationOptionText));
			}

			if (!RotationCommandLineText.IsEmpty())
			{
				bool bParsed = false;
				if (TryParseDifficultyBool(RotationCommandLineText, bParsed))
				{
					Resolution.bAllowRotation = bParsed;
					Resolution.RotationSource = EDifficultySource::CommandLine;
					return Resolution;
				}
				Resolution.IgnoredInputs.Add(
					DescribeInput(EDifficultySource::CommandLine, RotationNames, RotationCommandLineText));
			}

			Resolution.bAllowRotation = bPropertyAllowRotation;
			Resolution.RotationSource = EDifficultySource::Property;
		}

		return Resolution;
	}

	const TCHAR* DifficultySourceToText(EDifficultySource Source)
	{
		switch (Source)
		{
		case EDifficultySource::UrlOption:
			return TEXT("URL オプション");
		case EDifficultySource::CommandLine:
			return TEXT("コマンドライン");
		case EDifficultySource::Property:
			return TEXT("プロパティ");
		}
		return TEXT("不明");
	}
}
