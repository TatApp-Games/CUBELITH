// 領域拡張（Region Growing）による分割（RULES.md 3.2）。移植元は WebMock/src/core/generate.ts
// 同じ条件とシードなら Web 版と UE 版で同じパズルを出す（RULES.md 3.6）ので、乱数の消費の順を TS から変えない
// 命名は 001〜004 が決めた約束に揃える（namespace Cubelith・型は F 接頭辞・関数は WebMock と同じ名前を PascalCase に）
// 散らし（scatterPlacements）はこのファイルにはまだ無い（006 で足す）

#pragma once

#include "CoreMinimal.h"
#include "Grid.h"
#include "Piece.h"

namespace Cubelith
{
	/** 空間サイズ N の下限（RULES.md 3.1。TS の MIN_SPACE_SIZE） */
	inline constexpr int32 MinSpaceSize = 3;

	/** 空間サイズ N の上限（RULES.md 3.1。TS の MAX_SPACE_SIZE） */
	inline constexpr int32 MaxSpaceSize = 7;

	/** 分割数 M の下限（RULES.md 3.1。TS の MIN_PIECE_COUNT） */
	inline constexpr int32 MinPieceCount = 2;

	/**
	 * 生成結果（TS の GeneratedPuzzle）。
	 * Pieces は局所座標へ正規化済み（局所原点が (0,0,0)）で、Solution は各ピースの解答配置。
	 * Pieces[i] と Solution[i] は同じピース（id は i）を指す。
	 */
	struct CUBELITHCORE_API FGeneratedPuzzle
	{
		int32 N = 0;
		int32 M = 0;
		/** シードは uint32。照合データの puzzleSeeds に 4294967295 があり int32 では表せない（TS は seed >>> 0） */
		uint32 Seed = 0;
		TArray<FPiece> Pieces;
		TArray<FPlacement> Solution;
	};

	/**
	 * 空間サイズ N に対して選べる分割数 M の上限（RULES.md 3.1。TS の maxPieces）。
	 *
	 * M のプリセットは N から N−2 刻みの 5 段なので、上限はその最大値 N + 4(N−2) = 5N − 8
	 * （N=3 → 7、N=4 → 12、N=5 → 17、N=6 → 22、N=7 → 27）。
	 * N が 3..7 の外なら checkf（TS は RangeError）。
	 */
	CUBELITHCORE_API int32 MaxPieces(int32 N);

	/**
	 * 領域拡張で N×N×N を M 個の連結なピースに分割する（RULES.md 3.2。TS の generatePuzzle）。
	 * 返す Pieces は局所座標に正規化済みで、絶対座標（解答位置）は Solution が持つ。
	 * N が 3..7 の外、M が 2..MaxPieces(N) の外なら checkf（TS は RangeError）。
	 */
	CUBELITHCORE_API FGeneratedPuzzle GeneratePuzzle(int32 N, int32 M, uint32 Seed);

	/** 領域拡張で分割したピース（局所座標に正規化済み）。解答位置は SolutionPlacements で得る（TS の generatePieces） */
	CUBELITHCORE_API TArray<FPiece> GeneratePieces(int32 N, int32 M, uint32 Seed);

	/**
	 * 各ピースの解答配置（向きは恒等、位置は生成時の絶対座標のオフセット。TS の solutionPlacements）。
	 *
	 * 解釈（TS のまま）: 正規化でオフセットが落ちるため FPiece だけからは解答位置を復元できない。
	 * そこで GeneratePuzzle が Pieces と Solution を対で返す形にし、この関数は同じ引数で
	 * 解答配置だけを取り出す薄い入口とする（両方要るときは GeneratePuzzle を 1 回呼べばよい）。
	 */
	CUBELITHCORE_API TArray<FPlacement> SolutionPlacements(int32 N, int32 M, uint32 Seed);
}
