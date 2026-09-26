// ゲームの既定の GameMode。既定の Pawn を軌道カメラ（ACubelithOrbitPawn）にする
// Config/DefaultEngine.ini の GlobalDefaultGameMode がこのクラスを指している

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "CubelithGameMode.generated.h"

/**
 * 生成・散らし・ピースの表示は後続の段階（Docs/SPEC_UE.md 8 章の U2）でここに足す。
 * PlayerController と HUD は既定のまま使う。
 */
UCLASS()
class CUBELITH_API ACubelithGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACubelithGameMode();
};
