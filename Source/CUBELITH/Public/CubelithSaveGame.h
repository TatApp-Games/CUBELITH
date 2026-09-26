// セーブデータのスロットへの読み書き（RULES.md 3.8・Docs/SPEC_UE.md 4 章）。
// 保存する形と検証・更新の純粋関数は CubelithSave.h で、ここはその中身を USaveGame に乗せて
// UGameplayStatics::LoadGameFromSlot / SaveGameToSlot でやり取りする薄い層だけを持つ
//
// 解釈: Automation Test では実際のスロットへ読み書きしない（エディタの Saved/ を汚さないため）。
// テストは CubelithSave.h の純粋関数を直接呼ぶ

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "CubelithSave.h"

#include "CubelithSaveGame.generated.h"

/**
 * セーブデータの器（RULES.md 3.8 の 3 つ = 最後に選んだ難易度・途中の盤面・クリア回数）。
 *
 * 解釈: 中身は FCubelithSaveData 1 つに束ねて持つ。検証・更新の純粋関数（CubelithSave.h）が扱うのと
 * 同じ形をそのまま保存するので、読み書きのたびに写し替えを挟まなくて済む。
 */
UCLASS()
class CUBELITH_API UCubelithSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/**
	 * 保存する形の版番号（Cubelith::SaveVersion）。
	 * 想定外の版のデータは中身を信用せず、まるごと既定値へ落とす（Cubelith::LoadSaveData）
	 */
	UPROPERTY()
	int32 Version = Cubelith::SaveVersion;

	/** 最後に選んだ難易度・途中の盤面（有無のフラグ + 中身）・クリア回数 */
	UPROPERTY()
	FCubelithSaveData Data;
};

namespace Cubelith
{
	/** セーブデータのスロット名（Docs/SPEC_UE.md 4 章）。`Saved/SaveGames/CubelithSave.sav` になる */
	inline constexpr const TCHAR* SaveSlotName = TEXT("CubelithSave");

	/** スロットのユーザー index。ローカルの 1 人用なので常に 0（Docs/SPEC_UE.md 4 章） */
	inline constexpr int32 SaveUserIndex = 0;

	/**
	 * スロットから読む（TS の loadSave）。
	 * スロットが無い / 読めない / 版が違う / 難易度が使えない形なら既定値、
	 * 途中の盤面だけが使えない形なら盤面だけ捨てる（Cubelith::SanitizeSaveData）。例外は出さない
	 */
	CUBELITH_API FCubelithSaveData LoadSaveData();

	/**
	 * スロットへ書く（TS の storeSave）。書けたら true。
	 * 書けなくても遊びは続けられるので、失敗は false と警告ログだけで済ませる
	 */
	CUBELITH_API bool StoreSaveData(const FCubelithSaveData& Data);
}
