// 乱数シードの外部指定の解釈（RULES.md 3.1「検証のために外から指定できるようにする」・Docs/SPEC_UE.md 7.7）
// 文字列と int64 だけを受け取る純粋な関数として切り出してある。マップ URL のオプションを読む部分
// （UGameplayStatics::ParseOption）とコマンドラインを読む部分（FCommandLine）は呼び出し側
// （ACubelithGameMode）に残し、ここは解釈だけを持つのでテストから直接呼べる

#pragma once

#include "CoreMinimal.h"

namespace Cubelith
{
	/** シードとして受け付ける範囲（Web 版 `WebMock/src/ui/params.ts` の MIN_SEED / MAX_SEED と同じ） */
	inline constexpr int64 MinSeedValue = 0;
	inline constexpr int64 MaxSeedValue = static_cast<int64>(MAX_uint32);

	/** シードがどの経路で決まったか（Docs/SPEC_UE.md 7.7 の優先順位） */
	enum class ESeedSource : uint8
	{
		/** マップ URL のオプション `?seed=` */
		UrlOption,
		/** コマンドライン引数 `-CubelithSeed=` */
		CommandLine,
		/** ACubelithGameMode の UPROPERTY `Seed` */
		Property,
		/** どれも無い / どれも不正だったのでランダムに引いた */
		Random,
	};

	/** ResolveSeed の結果 */
	struct FSeedResolution
	{
		/** 生成と初期散らしに渡すシード */
		uint32 Seed = 0;

		/** Seed がどこから来たか */
		ESeedSource Source = ESeedSource::Random;

		/**
		 * 不正なので無視した指定（`?seed=12a` のような、そのままログに出せる形）。
		 * 空なら無視したものは無い。呼び出し側が 1 行の警告にまとめて出す
		 */
		TArray<FString> IgnoredInputs;
	};

	/**
	 * シードとして読める文字列なら OutSeed に入れて true。
	 *
	 * 受け付けるのは 0..4294967295 の 10 進整数だけ。
	 * 解釈: 符号（`+1` / `-1`）と前後の空白は受け付けない。Web 版（params.ts）は URL クエリを
	 * `URLSearchParams` 経由で読むため空白や符号が混じりうるが、こちらのマップ URL オプションと
	 * コマンドラインは空白でトークンが切れるので、混じるとしたら打ち間違いのときだけ。
	 * 黙って解釈するより無視して警告を出したほうが気付ける。桁数は制限しないので `007` は 7 になる。
	 */
	CUBELITH_API bool TryParseSeed(const FString& Text, uint32& OutSeed);

	/**
	 * 3 つの指定とランダムの控えから、実際に使うシードを決める（Docs/SPEC_UE.md 7.7）。
	 * 優先順位は URL オプション > コマンドライン > プロパティ > ランダムで、
	 * 不正な値は無視して次の順位へ落ちる（Web 版と同じ「不正値は乱数へフォールバック」）。
	 *
	 * @param UrlOptionText     マップ URL の `?seed=` の値。未指定なら空文字
	 * @param CommandLineText   `-CubelithSeed=` の値。未指定なら空文字
	 * @param PropertySeed      ACubelithGameMode の Seed。負の値は「未指定」
	 * @param RandomSeed        どれも使えなかったときに使う値（呼び出し側が引く）
	 *
	 * 解釈: 空文字は「指定なし」と区別できない（ParseOption は未指定でも空文字を返す）ので、
	 * 無視した指定としては数えず警告も出さない。プロパティの負の値も同じく未指定の扱い。
	 */
	CUBELITH_API FSeedResolution ResolveSeed(
		const FString& UrlOptionText, const FString& CommandLineText, int64 PropertySeed, uint32 RandomSeed);

	/** ESeedSource をログに出す日本語の名前 */
	CUBELITH_API const TCHAR* SeedSourceToText(ESeedSource Source);
}
