// ヒント対象のピース選定（RULES.md 3.7）。移植元は WebMock/src/core/hint.ts
// ヒントは「選ばれたピースを解答の位置・向きへ置いて固定する」機能で、ここはその対象選びだけを担う
// ヒントは乱数を使わない（RULES.md 3.6）ので Rng.h には依存しない
// 命名は 001〜006 が決めた約束に揃える（namespace Cubelith・型は F 接頭辞・関数は WebMock と同じ名前を PascalCase に）

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Misc/Optional.h"
#include "Piece.h"

namespace Cubelith
{
	/**
	 * ヒントで正解位置へ送るピースの id を選ぶ（TS の pickHintPiece）。使えるヒントが無ければ未設定。
	 *
	 * - 未固定（LockedIds に含まれない）のピースが 1 個以下なら未設定。
	 *   最後の 1 ピースをヒントで埋めると操作せずクリアできてしまうため（RULES.md 3.7）。
	 * - 未固定のうち、現在の配置が解答と異なる（位置と向きのどちらかが違う）ものの中で id が最小のものを選ぶ。
	 * - 未固定がすべて解答と一致していれば、未固定の id が最小のものを選ぶ。
	 *
	 * 同じ盤面なら常に同じピースを返す（乱数を使わない）。
	 *
	 * TS は number | null を返す。UE では正常系の null を TOptional で表す（U1 共通の約束）。
	 * LockedIds は TS の Iterable<number>。「含むかどうか」しか見ないので、呼び出し側が作りやすい
	 * TArrayView<const int32> で受ける（照合テストは選ばれた順の TArray<int32> をそのまま渡せる）。
	 * Solution に無い id が Placements にあれば呼び出し側のバグなので checkf（TS は Error）。
	 */
	CUBELITHCORE_API TOptional<int32> PickHintPiece(
		TArrayView<const FPlacement> Placements, TArrayView<const FPlacement> Solution, TArrayView<const int32> LockedIds);
}
