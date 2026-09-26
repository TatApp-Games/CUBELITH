// クリア判定（RULES.md 3.4）とマグネット・スナップの候補（RULES.md 3.5）。移植元は WebMock/src/core/solve.ts
// 物理判定は使わず、配列のインデックス計算と集合の照合だけで行う
// 命名は 001 / 002 / 003 が決めた約束に揃える（namespace Cubelith・型は F 接頭辞・関数は WebMock と同じ名前を PascalCase に）

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Misc/Optional.h"
#include "Grid.h"
#include "Piece.h"

namespace Cubelith
{
	/**
	 * 全ピースが N×N×N の立方体にぴったり収まっているか（TS の isSolved）。
	 *
	 * 変換後のボクセル総数が N³、重複が無く、外接ボックスの各辺がちょうど N ならクリア。
	 * 立方体の位置（原点）は問わない。ピース群がどこにあってもよい。
	 *
	 * Pieces と Placements はピース id で対応付ける（並び順は問わない）。
	 * id の重複・未知の id・配置漏れ / 二重配置は呼び出し側のバグなので checkf（TS は Error）。
	 */
	CUBELITHCORE_API bool IsSolved(TArrayView<const FPiece> Pieces, TArrayView<const FPlacement> Placements, int32 N);

	/**
	 * アクティブなピースの吸着先を返す（RULES.md 3.5。TS の snapCandidate）。無ければ未設定の TOptional。
	 *
	 * 現在位置から各軸 -1 / 0 / +1 の平行移動を試し、次をすべて満たす位置を候補にする。
	 *   1. 他のピースと少なくとも 1 面で接する（6 近傍で隣り合うボクセルの組が 1 つ以上ある）
	 *   2. 他のピースと重ならない
	 *   3. 全ピースのボクセルの外接立方体が N×N×N に収まる（各辺が N 以下。原点は問わない）
	 * 複数あればマンハッタン距離が最小のもの、同点は座標の辞書順で決定的に選ぶ。
	 *
	 * 向きは変えない。解釈: RULES.md 3.5 は位置についてのみ述べており、回転を伴う吸着は書かれていない。
	 * 解釈: 「既に置かれている他のピース」を区別する状態は持たないので、アクティブ以外の全ピースを相手にする。
	 * 現在位置がそのまま条件を満たすときは移動量 0 の候補（＝現在の配置と同じ位置）を返す。
	 *
	 * TS は Placement | null を返す。UE では正常系の null を TOptional で表す（U1 共通の約束）。
	 * Placements に含まれるピースだけを対象にする。id の重複・未知の id・
	 * アクティブなピースの配置漏れは呼び出し側のバグなので checkf（TS は Error）。
	 */
	CUBELITHCORE_API TOptional<FPlacement> SnapCandidate(
		TArrayView<const FPiece> Pieces, TArrayView<const FPlacement> Placements, int32 ActivePieceId, int32 N);
}
