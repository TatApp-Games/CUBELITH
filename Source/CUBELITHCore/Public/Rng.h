// シード付き乱数（mulberry32）。移植元は WebMock/src/core/rng.ts。生成結果の再現に使う
// 命名の約束（U1 共通・002 以降も踏襲する）: namespace Cubelith の中に置き、型は F 接頭辞・列挙は E 接頭辞、
// 関数は WebMock と同じ名前を PascalCase に直す（createRng → CreateRng）。Source/CLAUDE.md 開発ルール 2・10 から決めた

#pragma once

#include "CoreMinimal.h"

namespace Cubelith
{
	/** mulberry32 の乱数。CreateRng で作る（TS の createRng が返す Rng に対応） */
	struct CUBELITHCORE_API FRng
	{
		/** 内部状態。TS の `let a = seed >>> 0` に対応する */
		uint32 A = 0;

		/** mulberry32 の生の 32 bit 値。Next() が 4294967296 で割る前の値（照合データの uint32 列と直接比べられる） */
		uint32 NextUint32();

		/** [0, 1) の一様乱数 */
		double Next();

		/** [0, N) の整数。N は正の整数（TS は RangeError、こちらは checkf） */
		int32 NextInt(int32 N);
	};

	/** シードから乱数を作る（TS の createRng）。TS の `seed >>> 0` に合わせてシードは uint32 で受ける */
	CUBELITHCORE_API FRng CreateRng(uint32 Seed);
}
