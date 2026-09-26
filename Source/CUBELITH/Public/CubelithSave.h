// セーブデータの「保存する形」と、その検証・更新の純粋関数（RULES.md 3.8・Docs/SPEC_UE.md 4 章）。
// 移植元は WebMock/src/ui/save.ts。UE 版は JSON ではなく USaveGame のシリアライズなので、TS の
// parseSaveData / serializeSaveData はそのまま移さず、「読み込んだ値が使える形か検証する」関数として移植する
// （型の食い違いはシリアライザが防ぐので、残る危険は範囲外・食い違い・手で書き換えられた値だけ）。
//
// USTRUCT / UENUM は名前空間に入れられないのでグローバルに置き（F / E + Cubelith の接頭辞）、
// 関数は他のファイルと揃えて namespace Cubelith に置く。スロットへの読み書きは CubelithSaveGame.h
//
// 解釈: 選択中のピースとカメラの位置は保存しない（TS のまま。再開時は「選択なし・カメラは初期位置」でよい）
// 解釈: 盤面はピース形状ごとではなく「難易度 + シード + 配置 + 固定」で保存する。形は GeneratePuzzle(N, M, Seed)
// で再現できる（RULES.md 3.6）ので保存量が小さく、生成規則が変わったときにはピース数の食い違いとして検出できる
// 解釈: 残りピース数は保存する。タイトル画面では盤面を再生成せずに「途中の盤面あり」を出したい

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Game.h"
#include "Piece.h"

#include "CubelithSave.generated.h"

/**
 * 固定の種類（Cubelith::ELockKind に対応する保存用の形）。
 * ELockKind は CUBELITHCore にあり UObject を持てない（Docs/SPEC_UE.md 7.1）ので、保存には
 * UENUM の別の型を使い、ToCoreLockKind / ToSavedLockKind で相互に変換する。
 */
UENUM()
enum class ECubelithSavedLockKind : uint8
{
	/** 人が固定したもの（解除できる） */
	Manual = 0,
	/** ヒントで正解位置へ送った印（解除できない） */
	Hint = 1,
};

/** 難易度（RULES.md 3.1 の N / M / パズルの回転）。シードは難易度に含めない（TS の SavedDifficulty） */
USTRUCT()
struct CUBELITH_API FCubelithSavedDifficulty
{
	GENERATED_BODY()

	/** 空間サイズ N */
	UPROPERTY()
	int32 SpaceSize = 3;

	/** ピース分割数 M */
	UPROPERTY()
	int32 PieceCount = 4;

	/** パズルの回転 */
	UPROPERTY()
	bool bAllowRotation = false;
};

inline bool operator==(const FCubelithSavedDifficulty& A, const FCubelithSavedDifficulty& B)
{
	return A.SpaceSize == B.SpaceSize && A.PieceCount == B.PieceCount && A.bAllowRotation == B.bAllowRotation;
}

inline bool operator!=(const FCubelithSavedDifficulty& A, const FCubelithSavedDifficulty& B)
{
	return !(A == B);
}

/** 配置 1 つ分（TS の Placement）。ロジックの Cubelith::FPlacement と同じ意味で、座標だけ FIntVector で持つ */
USTRUCT()
struct CUBELITH_API FCubelithSavedPlacement
{
	GENERATED_BODY()

	UPROPERTY()
	int32 PieceId = 0;

	/** 向き id（0..23） */
	UPROPERTY()
	int32 Orientation = 0;

	/** ピースの局所原点のワールド座標（ロジックの整数グリッド。Docs/SPEC_UE.md 7.2 の変換前） */
	UPROPERTY()
	FIntVector Position = FIntVector::ZeroValue;
};

inline bool operator==(const FCubelithSavedPlacement& A, const FCubelithSavedPlacement& B)
{
	return A.PieceId == B.PieceId && A.Orientation == B.Orientation && A.Position == B.Position;
}

inline bool operator!=(const FCubelithSavedPlacement& A, const FCubelithSavedPlacement& B)
{
	return !(A == B);
}

/** 固定中のピース 1 つ分（TS の SavedLock） */
USTRUCT()
struct CUBELITH_API FCubelithSavedLock
{
	GENERATED_BODY()

	UPROPERTY()
	int32 PieceId = 0;

	UPROPERTY()
	ECubelithSavedLockKind Kind = ECubelithSavedLockKind::Manual;
};

inline bool operator==(const FCubelithSavedLock& A, const FCubelithSavedLock& B)
{
	return A.PieceId == B.PieceId && A.Kind == B.Kind;
}

inline bool operator!=(const FCubelithSavedLock& A, const FCubelithSavedLock& B)
{
	return !(A == B);
}

/** 中断した盤面（TS の SavedProgress）。Seed と難易度があればピースの形は生成し直せる */
USTRUCT()
struct CUBELITH_API FCubelithSavedProgress
{
	GENERATED_BODY()

	UPROPERTY()
	FCubelithSavedDifficulty Difficulty;

	/**
	 * 盤面のシード。
	 * 解釈: シードは uint32 だが UPROPERTY にできないので int64 で持つ（ACubelithGameMode::Seed と同じ扱い）。
	 * 負の値は「保存されていない」とみなし、検証で弾く
	 */
	UPROPERTY()
	int64 Seed = -1;

	/** 全ピースの配置（ピース数は Difficulty.PieceCount と一致すること） */
	UPROPERTY()
	TArray<FCubelithSavedPlacement> Placements;

	/** 固定中のピースだけ。id 昇順 */
	UPROPERTY()
	TArray<FCubelithSavedLock> Locks;

	/** タイトルに出す残りピース数（未確定のピース数。CubelithProgress.h） */
	UPROPERTY()
	int32 Remaining = 0;
};

/** クリア回数（TS の SavedClears）。合計と難易度ごと */
USTRUCT()
struct CUBELITH_API FCubelithSavedClears
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Total = 0;

	/** 難易度ごとの回数。キーは Cubelith::DifficultyKey の形（`"3-4-0"`） */
	UPROPERTY()
	TMap<FString, int32> ByDifficulty;
};

/**
 * 保存する中身（TS の SaveData から version を除いたもの。RULES.md 3.8 の 3 つ）。
 * 版番号は UCubelithSaveGame が持つ（CubelithSaveGame.h）。
 */
USTRUCT()
struct CUBELITH_API FCubelithSaveData
{
	GENERATED_BODY()

	/** 最後に選んだ難易度 */
	UPROPERTY()
	FCubelithSavedDifficulty Difficulty;

	/** 中断した盤面があるか（TS の progress が null かどうかに当たる） */
	UPROPERTY()
	bool bHasProgress = false;

	/** 中断した盤面。bHasProgress が false のときの中身は見ない */
	UPROPERTY()
	FCubelithSavedProgress Progress;

	/** クリア回数 */
	UPROPERTY()
	FCubelithSavedClears Clears;
};

namespace Cubelith
{
	/**
	 * 保存する形の版番号（TS の SAVE_KEY に含まれる `v1` に当たる）。
	 * 形を変えたら 1 つ上げる。想定外の版のデータは読み捨てて既定値へ落とす（CubelithSaveGame.h）
	 */
	inline constexpr int32 SaveVersion = 1;

	/** 何も遊んでいない状態（RULES.md 3.1 の既定 N=3 / M=4 / 回転なし・途中の盤面なし・クリア回数 0。TS の defaultSaveData） */
	CUBELITH_API FCubelithSaveData DefaultSaveData();

	/**
	 * 難易度ごとの集計キー。例 `"3-4-0"`（パズルの回転ありは `1`。TS の difficultyKey）。
	 * TMap のキーになるので、順序と区切りを固定した安定した文字列にする
	 */
	CUBELITH_API FString DifficultyKey(const FCubelithSavedDifficulty& Difficulty);

	/** その難易度のクリア回数（記録が無ければ 0。TS の clearCountOf） */
	CUBELITH_API int32 ClearCountOf(const FCubelithSavedClears& Clears, const FCubelithSavedDifficulty& Difficulty);

	/** 同じもののセーブ全体からの引き方（呼ぶ側が Clears を取り出さなくて済むようにした別名） */
	CUBELITH_API int32 ClearCountOf(const FCubelithSaveData& Data, const FCubelithSavedDifficulty& Difficulty);

	/**
	 * クリアを 1 回記録する（合計と難易度ごとを 1 増やす。TS の withClearRecorded）。
	 * 「最後に選んだ難易度」はクリアした難易度へ揃え、クリアした盤面は「途中」ではないので捨てる（RULES.md 3.8）
	 */
	CUBELITH_API void RecordClear(FCubelithSaveData& Data, const FCubelithSavedDifficulty& Difficulty);

	/** 難易度が使える形か（N は MinSpaceSize..MaxSpaceSize、M は MinPieceCount..MaxPieces(N)。TS の parseDifficulty） */
	CUBELITH_API bool IsValidSavedDifficulty(const FCubelithSavedDifficulty& Difficulty);

	/**
	 * 中断した盤面が使える形か（TS の parseProgress + ピース数と M の突き合わせ）。
	 * 難易度が使える形で、シードが 0..4294967295、配置が 1 個以上で PieceId に重複が無く向きが 0..23、
	 * 固定の id がすべて配置にあって重複が無く、Remaining が 0..配置数、配置数が難易度の M と一致すること
	 */
	CUBELITH_API bool IsValidSavedProgress(const FCubelithSavedProgress& Progress);

	/**
	 * 保存された盤面をそのまま初期配置として使えるか（TS の progressFitsPieces）。
	 * 条件は Cubelith::FGame が受け付けるもの（全ピースの id がちょうど 1 回ずつ現れる）と同じ。
	 *
	 * 生成規則が変わった / 手で書き換えられた場合にここで弾き、復元をあきらめて通常の散らしで始められるようにする
	 */
	CUBELITH_API bool ProgressFitsPieces(const FCubelithSavedProgress& Progress, TArrayView<const int32> PieceIds);

	/**
	 * 読み込んだデータを使える形へ直す（TS の parseSaveData に当たる）。
	 *
	 * - 難易度が使えない形 → まるごと既定値（どの難易度で遊んでいたか分からないので他も信用しない）
	 * - 途中の盤面が使えない形 → **盤面だけ捨てて難易度とクリア回数は生かす**（TS と同じ）。
	 *   解釈: TS はピース数と M の食い違いだけをこの扱いにし、他の壊れ方ではまるごと捨てるが、
	 *   UE 版はシリアライザが型を保証するので残る壊れ方は「範囲外・食い違い」だけになる。
	 *   どれも盤面の中身の問題なので、盤面だけを捨てる扱いに寄せた
	 * - クリア回数 → 負の合計は 0 に、負の回数のキーは落とす（回数の記録は遊びの進行に影響しないので、
	 *   全体を捨てるより残すほうが損が小さい。TS の parseClears と同じ思想）
	 */
	CUBELITH_API FCubelithSaveData SanitizeSaveData(const FCubelithSaveData& Data);

	/** 保存用の固定の種類をロジックの ELockKind へ */
	CUBELITH_API ELockKind ToCoreLockKind(ECubelithSavedLockKind Kind);

	/** ロジックの ELockKind を保存用の形へ */
	CUBELITH_API ECubelithSavedLockKind ToSavedLockKind(ELockKind Kind);

	/** 保存用の配置をロジックの FPlacement へ */
	CUBELITH_API FPlacement ToCorePlacement(const FCubelithSavedPlacement& Placement);

	/** ロジックの FPlacement を保存用の形へ */
	CUBELITH_API FCubelithSavedPlacement ToSavedPlacement(const FPlacement& Placement);

	/** 保存された配置をロジックの初期配置（Cubelith::FGame へ渡す形）へ直す。並びは保存された順のまま */
	CUBELITH_API TArray<FPlacement> ToCorePlacements(TArrayView<const FCubelithSavedPlacement> Placements);

	/**
	 * 固定中のピースを保存する形にする（id 昇順。TS の main.ts の currentLocks）。
	 * FGame::LockedIds は昇順なので並べ直しは要らない（SanitizeSaveData が期待する並びと同じ）
	 */
	CUBELITH_API TArray<FCubelithSavedLock> CollectSavedLocks(const FGame& Game);

	/**
	 * 今の盤面から「途中の盤面」を組み立てる（TS の main.ts の saveProgress が作る SavedProgress）。
	 * Seed は uint32 のまま受けて int64 へ入れる（FCubelithSavedProgress::Seed の「解釈:」）
	 */
	CUBELITH_API FCubelithSavedProgress MakeSavedProgress(
		const FCubelithSavedDifficulty& Difficulty, uint32 Seed,
		TArrayView<const FPlacement> Placements, TArrayView<const FCubelithSavedLock> Locks, int32 Remaining);

	/**
	 * 途中の盤面を覚える（TS の withProgress）。**最後に選んだ難易度もその盤面の難易度に揃える**
	 * （難易度が決まるのはタイトルで「開始」または「続きから」を押したとき。RULES.md 3.8）。
	 * 覚えていた盤面は確認なしで上書きされる
	 */
	CUBELITH_API void SetProgress(FCubelithSaveData& Data, const FCubelithSavedProgress& Progress);
}
