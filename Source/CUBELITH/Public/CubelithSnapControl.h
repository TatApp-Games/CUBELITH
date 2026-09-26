// 手を離したときのマグネット・スナップの制御（RULES.md 3.5 / 5.1。Docs/SPEC_UE.md 8 章 U4）
// 移植元は WebMock/src/input/snapControl.ts の createSnapControl。
// 候補を求めるのは CUBELITHCore の純粋関数 Cubelith::SnapCandidate で、ここは
// 「いつ呼ぶか」と結果の配り先（光らせる対象の切り替え・吸着先の受け渡し）だけを持つ。

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Misc/Optional.h"

#include "Piece.h"

namespace Cubelith
{
	/** 吸着 1 回分。From が吸着前の配置、To が吸着後の配置（TS の onSnap の引数） */
	struct FSnapTarget
	{
		FPlacement From;
		FPlacement To;
	};

	/**
	 * スナップの制御（TS の SnapControl）。ゲーム状態は持たず、呼び出しごとに配置を受け取る。
	 *
	 * 解釈: TS はクロージャで placements() を握るが、C++ では呼び出しごとに引数で受ける形にした
	 * （FGame への参照を持たずに済み、テストで配置を手で組める）。ピースと N は構築時に保持する。
	 * Docs/SPEC_UE.md 7.1 のとおり UObject にはしない（純粋な状態機械で、持つのは「今光らせている対象」だけ）。
	 */
	class CUBELITH_API FSnapControl
	{
	public:
		/** ピースも N も無い空の状態。パズルを開く前の置き場所として使う（Refresh / Release は何もしない） */
		FSnapControl() = default;

		/** Pieces は写して持つ（TS が options.pieces を握るのと同じ）。N は解答空間のサイズ */
		FSnapControl(TArrayView<const FPiece> InPieces, int32 InN);

		/**
		 * 光らせる対象を計算し直す（TS の refresh）。**変わったときだけ true**（TS が onHintChange を
		 * 出さないのと同じ）。配置が変わった / 選択が変わった / 回転が確定したタイミングで呼ぶもので、
		 * 毎フレーム呼ぶ必要は無い。
		 *
		 * PieceId が INDEX_NONE なら「対象なし」（TS の pieceId === null）。固定中のピースを弾くのは
		 * 呼び出し側の役目で、TS の main.ts が lockKindOf を見て null を渡しているのと同じ
		 * （ACubelithPlayerController がそろえている）。
		 */
		bool Refresh(TArrayView<const FPlacement> Placements, int32 PieceId);

		/**
		 * 手を離した時点で呼ぶ（TS の release）。吸着先があれば From / To を返す。無ければ未設定の TOptional。
		 * 離した時点の配置で計算し直す（Refresh の後にドラッグが進んでいることがある）。
		 * 吸着したかどうかによらず、光らせる対象はここで「なし」に戻る（HintedPieceId で読める）。
		 */
		TOptional<FSnapTarget> Release(TArrayView<const FPlacement> Placements, int32 PieceId);

		/** 今光らせている対象のピース id（無ければ INDEX_NONE。TS の hinted） */
		int32 HintedPieceId() const { return HintedPiece; }

		/** 解答空間のサイズ N（構築時に受けた値。空の状態では 0） */
		int32 N() const { return SpaceSize; }

	private:
		/**
		 * 吸着先を求める（TS の compute）。PieceId が INDEX_NONE・配置に無い・候補が無いなら未設定。
		 * **現在位置がそのまま候補になる（＝すでに収まっている）ときは吸着先として扱わない**（TS と同じ）。
		 */
		TOptional<FPlacement> Compute(TArrayView<const FPlacement> Placements, int32 PieceId) const;

		/** 光らせる対象を置き換える。変わったときだけ true（TS の setHint） */
		bool SetHint(int32 PieceId);

		/** 構築時に写したピース（局所座標）。SnapCandidate に渡す */
		TArray<FPiece> PieceList;

		/** 解答空間のサイズ N */
		int32 SpaceSize = 0;

		/** 今光らせている対象（無ければ INDEX_NONE） */
		int32 HintedPiece = INDEX_NONE;
	};
}
