// 難易度（空間サイズ N・ピース分割数 M・パズルの回転）の外部指定の解釈（RULES.md 3.1・Docs/SPEC_UE.md 7.7）。
// CubelithSeed.h と同じ構えで、文字列と数値だけを受け取る純粋な関数として切り出してある。マップ URL の
// オプションを読む部分（UGameplayStatics::ParseOption）とコマンドラインを読む部分（FCommandLine）は
// 呼び出し側（ACubelithGameMode）に残し、ここは解釈だけを持つのでテストから直接呼べる。
//
// シード（CubelithSeed.h）との違いは範囲外の扱い。シードは「指定した値と盤面の 1 対 1 対応」を守るために
// 範囲外を捨てるが、難易度にその対応は要らないので **範囲外は丸める**（Web 版の `?n=` / `?m=` と同じ。
// WebMock/src/ui/params.ts の clampInt / WebMock/src/ui/difficulty.ts の nearestPreset）。

#pragma once

#include "CoreMinimal.h"

namespace Cubelith
{
	/** M のプリセットの段数（RULES.md 3.1。Web 版 `WebMock/src/ui/difficulty.ts` の PRESET_STEPS と同じ） */
	inline constexpr int32 PiecePresetSteps = 5;

	/** 難易度の項目がどの経路で決まったか（Docs/SPEC_UE.md 7.7 の優先順位） */
	enum class EDifficultySource : uint8
	{
		/** マップ URL のオプション `?n=` / `?m=` / `?rot=` */
		UrlOption,
		/** コマンドライン引数 `-CubelithN=` / `-CubelithM=` / `-CubelithRotation=` */
		CommandLine,
		/** ACubelithGameMode の UPROPERTY（`SpaceSize` / `PieceCount` / `bAllowRotation`） */
		Property,
	};

	/** ResolveDifficulty の結果。項目ごとに独立して決まるので、経路も項目ごとに持つ */
	struct FDifficultyResolution
	{
		/** 生成と初期散らしに渡す空間サイズ N（必ず MinSpaceSize..MaxSpaceSize） */
		int32 SpaceSize = 3;

		/** 生成に渡すピース分割数 M（必ず PiecePresets(SpaceSize) のどれか） */
		int32 PieceCount = 4;

		/** 初期散らしで向きもランダムにするか（RULES.md 3.1 の「パズルの回転」） */
		bool bAllowRotation = false;

		/** SpaceSize がどこから来たか */
		EDifficultySource SpaceSizeSource = EDifficultySource::Property;

		/** PieceCount がどこから来たか */
		EDifficultySource PieceCountSource = EDifficultySource::Property;

		/** bAllowRotation がどこから来たか */
		EDifficultySource RotationSource = EDifficultySource::Property;

		/**
		 * 読めなかったので無視した指定（`?n=12a` のような、そのままログに出せる形）。
		 * 空なら無視したものは無い。呼び出し側が 1 行の警告にまとめて出す（シードと同じ）
		 */
		TArray<FString> IgnoredInputs;

		/**
		 * 読めたが範囲外 / プリセット外だったので寄せた指定（`?n=9 → 7` のような形）。
		 * 空なら寄せたものは無い。これも呼び出し側が警告として出す
		 */
		TArray<FString> AdjustedInputs;
	};

	/**
	 * N / M として読める文字列なら OutValue に入れて true。範囲の丸めは ResolveDifficulty が行う。
	 *
	 * 受け付けるのは 10 進整数だけで、先頭の符号（`+4` / `-1`）は付いていてもよい。
	 * `12a`・`1.5`・`0x10`・空文字は不正。Web 版（params.ts の parseIntParam）と同じく、
	 * JavaScript の安全な整数（2^53 − 1）を超える桁数も不正として弾く。
	 *
	 * 解釈: シード（TryParseSeed）は符号を受け付けないが、こちらは受け付ける。シードは範囲外を捨てるので
	 * `-1` を弾くと「無視して乱数へ」になるが、難易度は範囲外を丸めるので、符号を弾くと Web 版で
	 * 3 になる `?n=-1` が UE 版では「無視して次の順位」になってずれる。前後の空白はどちらも受け付けない
	 * （マップ URL のオプションもコマンドラインも空白でトークンが切れるので、混じるとしたら打ち間違いのときだけ）。
	 */
	CUBELITH_API bool TryParseDifficultyInt(const FString& Text, int64& OutValue);

	/**
	 * パズルの回転として読める文字列なら OutValue に入れて true。
	 *
	 * 受け付けるのは `1` / `true` / `on` / `yes`（真）と `0` / `false` / `off` / `no`（偽）で、
	 * 大文字小文字は問わない（Web 版 params.ts の parseBoolParam と同じ語）。それ以外と空文字は不正。
	 */
	CUBELITH_API bool TryParseDifficultyBool(const FString& Text, bool& OutValue);

	/**
	 * N に対する M のプリセット（RULES.md 3.1 の表。5 段・N から N−2 刻み・最大 N + 4(N−2)）。
	 * Web 版 `WebMock/src/ui/difficulty.ts` の piecePresets と同じ結果を返す。
	 *
	 * N は MinSpaceSize..MaxSpaceSize であること（MaxPieces の checkf に落ちる）。
	 */
	CUBELITH_API TArray<int32> PiecePresets(int32 N);

	/**
	 * Presets のうち M に最も近いもの（Web 版 difficulty.ts の nearestPreset と同じ）。
	 *
	 * 解釈: 距離が同じなら小さい方を採る。PiecePresets は昇順で返し、ここは「今の候補より真に近い」ときだけ
	 * 差し替えるので、同距離では先に見た（= 小さい）方が残る。Web 版の実装と同じ挙動。
	 */
	CUBELITH_API int32 NearestPreset(const TArray<int32>& Presets, int32 M);

	/**
	 * 3 つの指定から、実際に使う難易度を決める（Docs/SPEC_UE.md 7.7）。
	 * 優先順位は URL オプション > コマンドライン > プロパティで、**項目ごとに独立して決める**
	 * （`?n=` だけ指定したら M と回転はプロパティの値）。読めない値は無視して次の順位へ落ちる。
	 *
	 * @param SpaceSizeOptionText        マップ URL の `?n=` の値。未指定なら空文字
	 * @param PieceCountOptionText       マップ URL の `?m=` の値
	 * @param RotationOptionText         マップ URL の `?rot=` の値
	 * @param SpaceSizeCommandLineText   `-CubelithN=` の値。未指定なら空文字
	 * @param PieceCountCommandLineText  `-CubelithM=` の値
	 * @param RotationCommandLineText    `-CubelithRotation=` の値
	 * @param PropertySpaceSize          ACubelithGameMode の SpaceSize（範囲外なら丸める）
	 * @param PropertyPieceCount         ACubelithGameMode の PieceCount（プリセット外なら寄せる）
	 * @param bPropertyAllowRotation     ACubelithGameMode の bAllowRotation
	 *
	 * 解釈: 空文字は「指定なし」と区別できない（ParseOption は未指定でも空文字を返す）ので、
	 * 無視した指定としては数えず警告も出さない（シードと同じ）。真偽値には「未指定」を表す値が無いので、
	 * プロパティの `bAllowRotation` は常に最後の受け皿になる（= 経路がランダムになることはない）。
	 * M は N が決まってからでないとプリセットが決まらないので、N → M の順に決める。
	 */
	CUBELITH_API FDifficultyResolution ResolveDifficulty(
		const FString& SpaceSizeOptionText,
		const FString& PieceCountOptionText,
		const FString& RotationOptionText,
		const FString& SpaceSizeCommandLineText,
		const FString& PieceCountCommandLineText,
		const FString& RotationCommandLineText,
		int32 PropertySpaceSize,
		int32 PropertyPieceCount,
		bool bPropertyAllowRotation);

	/** EDifficultySource をログに出す日本語の名前 */
	CUBELITH_API const TCHAR* DifficultySourceToText(EDifficultySource Source);
}
