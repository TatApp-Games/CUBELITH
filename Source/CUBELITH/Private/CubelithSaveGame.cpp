// CubelithSaveGame.h の実装。読めないときの扱いは移植元 WebMock/src/ui/save.ts の loadSave と同じく
// 「例外を出さず既定値へフォールバック」

#include "CubelithSaveGame.h"

#include "CubelithLog.h"
#include "Kismet/GameplayStatics.h"

namespace Cubelith
{
	FCubelithSaveData LoadSaveData()
	{
		const FString SlotName(SaveSlotName);
		if (!UGameplayStatics::DoesSaveGameExist(SlotName, SaveUserIndex))
		{
			return DefaultSaveData();
		}

		USaveGame* const Loaded = UGameplayStatics::LoadGameFromSlot(SlotName, SaveUserIndex);
		const UCubelithSaveGame* const SaveGame = Cast<UCubelithSaveGame>(Loaded);
		if (SaveGame == nullptr)
		{
			// 壊れている / 別のクラスで保存されている。既定値から始める（RULES.md 3.8）
			UE_LOG(LogCubelith, Warning,
				TEXT("セーブデータ（スロット %s）を読めなかったので既定値から始めます"), SaveSlotName);
			return DefaultSaveData();
		}
		if (SaveGame->Version != SaveVersion)
		{
			UE_LOG(LogCubelith, Warning,
				TEXT("セーブデータの版が想定外（%d、想定 %d）なので既定値から始めます"),
				SaveGame->Version, SaveVersion);
			return DefaultSaveData();
		}

		FCubelithSaveData Sanitized = SanitizeSaveData(SaveGame->Data);
		if (SaveGame->Data.bHasProgress && !Sanitized.bHasProgress)
		{
			UE_LOG(LogCubelith, Warning,
				TEXT("セーブデータの途中の盤面が使えない形だったので捨てました（難易度とクリア回数は残します）"));
		}
		return Sanitized;
	}

	bool StoreSaveData(const FCubelithSaveData& Data)
	{
		UCubelithSaveGame* const SaveGame = NewObject<UCubelithSaveGame>();
		SaveGame->Version = SaveVersion;
		SaveGame->Data = Data;

		const FString SlotName(SaveSlotName);
		if (!UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, SaveUserIndex))
		{
			UE_LOG(LogCubelith, Warning, TEXT("セーブデータ（スロット %s）を書けませんでした"), SaveSlotName);
			return false;
		}
		return true;
	}
}
