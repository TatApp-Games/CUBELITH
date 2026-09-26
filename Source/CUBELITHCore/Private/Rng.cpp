#include "Rng.h"

namespace Cubelith
{
	// TS の next() をそのまま移した。t を最初から最後まで uint32 で持てば JS と同じビット列になる（Docs/SPEC_UE.md 7.1）
	// - Math.imul(a, b) は 32 bit に切り捨てた乗算。uint32 同士の乗算がそのまま対応する（符号の解釈が違うだけでビット列は同じ）
	// - `>>>` は論理シフト。uint32 の `>>` が対応する（int32 で書くと算術シフトになって壊れる）
	// - `t ^= t + ...` の `+` は JS では倍精度だが、直後の `^=` が ToInt32 で 2^32 で丸めるので uint32 の折り返し加算と同じ
	uint32 FRng::NextUint32()
	{
		A = A + 0x6d2b79f5u;
		uint32 T = A;
		T = (T ^ (T >> 15)) * (T | 1u);
		T ^= T + (T ^ (T >> 7)) * (T | 61u);
		return T ^ (T >> 14);
	}

	double FRng::Next()
	{
		// [0, 1) への変換だけ double で行う（JS の number は倍精度）
		return static_cast<double>(NextUint32()) / 4294967296.0;
	}

	int32 FRng::NextInt(int32 N)
	{
		// TS が RangeError を投げていた「呼び出し側のバグ」は checkf にする（移植の約束）
		checkf(N > 0, TEXT("NextInt: N は正の整数 (got %d)"), N);
		// TS と同じ Math.floor(next() * n)。Next() の結果に N を掛けてから床関数（順序を変えない）
		return static_cast<int32>(FMath::FloorToDouble(Next() * static_cast<double>(N)));
	}

	FRng CreateRng(uint32 Seed)
	{
		FRng Rng;
		Rng.A = Seed;
		return Rng;
	}
}
