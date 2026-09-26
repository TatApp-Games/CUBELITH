// ゲーム状態の集約（RULES.md 3.3 / 3.4）。現在の配置を持ち、変更のたびにクリア判定を回す
// 移植元は WebMock/src/core/game.ts。描画への反映は OnChange のコールバックで外へ出す
// 命名は 001〜006 が決めた約束に揃える（namespace Cubelith・型は F 接頭辞・列挙は E 接頭辞・関数は WebMock と同じ名前を PascalCase に）

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"
#include "Grid.h"
#include "Piece.h"

namespace Cubelith
{
	/** 配置を Delta だけ平行移動した新しい配置（TS の movePlacement）。向きは変えない。 */
	CUBELITHCORE_API FPlacement MovePlacement(const FPlacement& Placement, const FVec3& Delta);

	/**
	 * 軸まわりに 90 度回した新しい配置（TS の rotatePlacement）。Dir は +1 / -1。
	 * Position はピースの局所原点のワールド座標なので、向き id を差し替えるだけで
	 * 「局所原点を中心に回す」（RULES.md 3.3）になる。
	 */
	CUBELITHCORE_API FPlacement RotatePlacement(const FPlacement& Placement, EAxis Axis, int32 Dir);

	/**
	 * Placements のうち Next と同じ id の配置を差し替えた新しい配列（TS の replacePlacement）。元の並びは保つ。
	 * 対象の id が無ければ呼び出し側のバグなので checkf（TS は Error）。
	 */
	CUBELITHCORE_API TArray<FPlacement> ReplacePlacement(TArrayView<const FPlacement> Placements, const FPlacement& Next);

	/**
	 * 固定（ロック）の種類（TS の LockKind）。
	 * Manual は人が固定したもので Unlock で解除できる。Hint はヒントで正解位置へ送った印で解除できない（RULES.md 3.3）。
	 */
	enum class ELockKind : uint8
	{
		Manual = 0,
		Hint = 1,
	};

	/**
	 * 配置が変わるたびに呼ばれる（TS の GameChangeListener）。bSolved はその時点のクリア判定の結果。
	 * TDelegate は CoreUObject を引き込まないが、CUBELITHCore は Core だけに依存するので
	 * 素の TFunction にする（ラムダをそのまま渡せる）。
	 */
	using FGameChangeListener = TFunction<void(TArrayView<const FPlacement> Placements, bool bSolved)>;

	/**
	 * 配置の保持と更新（TS の Game / createGame）。すべての更新経路がクリア判定を通る。
	 *
	 * 解釈: TS は「メソッドの束を返すクロージャ」だが、C++ ではクラスにする（TFunction の束を持つ構造体より素直）。
	 * Core だけに依存するので UObject にはしない（Docs/SPEC_UE.md 7.1）。
	 */
	class CUBELITHCORE_API FGame
	{
	public:
		/**
		 * ゲーム状態を作る。Initial は全ピースをちょうど 1 回ずつ含んでいること
		 * （未知の id・重複・数の不足は呼び出し側のバグなので checkf。TS は Error）。
		 * 構築時に IsSolved を 1 回走らせるが、OnChange は呼ばない
		 * （初回の描画は呼び出し側が Placements() から行う）。
		 */
		FGame(TArrayView<const FPiece> Pieces, int32 InN, TArrayView<const FPlacement> Initial,
			FGameChangeListener OnChange = FGameChangeListener());

		/** 全ピース（構築時に写したもの。TS の Game.pieces） */
		const TArray<FPiece>& Pieces() const { return PieceList; }

		/** 空間サイズ N（TS の Game.n）。TS の名前に揃えてある */
		int32 N() const { return SpaceSize; }

		/** 現在の全ピースの配置（順不同ではなく初期配置の並びを保つ。TS の placements()） */
		TArrayView<const FPlacement> Placements() const { return Current; }

		/** ピース id の配置。未知の id なら nullptr（TS の placementOf が返す undefined） */
		const FPlacement* PlacementOf(int32 PieceId) const;

		/** 直近の更新時点のクリア判定（TS の solved()） */
		bool Solved() const { return bSolvedFlag; }

		/** ピースを Delta だけ動かす（TS の move）。固定中は何もしない */
		void Move(int32 PieceId, const FVec3& Delta);

		/** ピースを軸まわりに 90 度回す（TS の rotate）。固定中は何もしない */
		void Rotate(int32 PieceId, EAxis Axis, int32 Dir);

		/**
		 * 位置と向きを直接指定して置く（ヒントの適用・回転スナップの確定に使う。TS の place）。固定中は何もしない。
		 * 向き id が 0..23 の外なら checkf（TS は RangeError）。
		 */
		void Place(int32 PieceId, int32 Orientation, const FVec3& Position);

		/** ピースを固定する（TS の lock）。固定中は Move / Rotate / Place が効かない。配置は変わらないので OnChange は呼ばない */
		void Lock(int32 PieceId, ELockKind Kind);

		/** Manual の固定を解除する（TS の unlock）。Hint の固定は解除できず何も起きない */
		void Unlock(int32 PieceId);

		/** 固定の種類。固定していなければ未設定（TS の lockKindOf が返す null） */
		TOptional<ELockKind> LockKindOf(int32 PieceId) const;

		/** 固定中のピース id（昇順。TS の lockedIds()） */
		TArray<int32> LockedIds() const;

		/** 配置をまるごと入れ替える（「散らし直す」。TS の reset）。Manual の固定は解除し Hint は保つ */
		void Reset(TArrayView<const FPlacement> Placements);

	private:
		/** 配置の集合が全ピースをちょうど 1 回ずつ含むか確かめ、写しを返す（TS の validate） */
		TArray<FPlacement> Validate(TArrayView<const FPlacement> Placements) const;

		/** 未知のピース id は呼び出し側のバグなので checkf（TS の assertKnown） */
		void AssertKnown(int32 PieceId) const;

		/** 配置を差し替え、クリア判定を回してから OnChange を呼ぶ（TS の apply） */
		void Apply(TArray<FPlacement>&& Next);

		/** 現在の配置。未知の id は呼び出し側のバグなので checkf（TS の placementFor） */
		const FPlacement& PlacementFor(int32 PieceId) const;

		TArray<FPiece> PieceList;
		int32 SpaceSize = 0;
		/** 既知のピース id。「含むかどうか」だけを見るので TSet でよい（TS の known） */
		TSet<int32> KnownIds;
		/** 現在の配置。並びが結果に効くので TArray（Docs/SPEC_UE.md 7.1） */
		TArray<FPlacement> Current;
		bool bSolvedFlag = false;
		/**
		 * 固定中のピース id → 固定の種類（TS の locks）。配置とは別に持つ（固定しても配置は変わらない）。
		 * Docs/SPEC_UE.md 7.1 は「順序が結果に効くところは TMap を使わない」としているが、ここは
		 * id 引きしかせず、唯一並びが外に出る LockedIds() が昇順にソートして返すので TMap でよい。
		 */
		TMap<int32, ELockKind> Locks;
		FGameChangeListener OnChangeListener;
	};

	/**
	 * ゲーム状態を作る（TS の createGame）。FGame の構築そのままで、TS と同じ入口の名前を残すために置いてある。
	 * 更新のたびに IsSolved を呼ぶ（RULES.md 3.4「判定はピースを動かすたびに実行」）。
	 */
	CUBELITHCORE_API FGame CreateGame(TArrayView<const FPiece> Pieces, int32 N, TArrayView<const FPlacement> Initial,
		FGameChangeListener OnChange = FGameChangeListener());
}
