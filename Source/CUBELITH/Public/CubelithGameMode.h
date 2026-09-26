// ゲームの既定の GameMode。既定の Pawn を軌道カメラ（ACubelithOrbitPawn）にし、
// ゲーム開始時にパズルを生成して散らし、ピースを表示して軌道カメラを立方体に合わせる（Docs/SPEC_UE.md 8 章 U2）
// Config/DefaultEngine.ini の GlobalDefaultGameMode がこのクラスを指している

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/GameModeBase.h"
#include "Templates/UniquePtr.h"

#include "Game.h"

#include "CubelithGameMode.generated.h"

class ACubelithPuzzleActor;

/**
 * 既定の難易度（RULES.md 3.1 の N=3・M=4・パズルの回転なし）でパズルを 1 つ作り、
 * ACubelithPuzzleActor に描かせて軌道カメラを合わせる。
 *
 * 難易度の選択・HUD・「次の問題」は U4 で足す。ここではエディタから差し替えられる既定値として持つ。
 * PlayerController と HUD は既定のまま使う。
 */
UCLASS()
class CUBELITH_API ACubelithGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACubelithGameMode();

	/** 空間サイズ N（RULES.md 3.1。既定 3） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle", meta = (ClampMin = "3", ClampMax = "7"))
	int32 SpaceSize = 3;

	/** ピース分割数 M（RULES.md 3.1。N=3 の既定は 4） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle", meta = (ClampMin = "2", ClampMax = "27"))
	int32 PieceCount = 4;

	/** パズルの回転（RULES.md 3.1。既定は「なし」= 全ピースを恒等の向きで散らす） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle")
	bool bAllowRotation = false;

	/**
	 * 乱数シード。−1（負の値）= 起動ごとにランダム、0..4294967295 = その値を uint32 として使う。
	 *
	 * 解釈: RULES.md 3.1 のシードは符号なし 32 bit だが、uint32 は UPROPERTY にできないので int64 で持つ。
	 * ランダムに引いたときは実際の値を LogCubelith に出すので、同じパズルを再現したいときは
	 * その値をここに入れる。外から（コマンドライン・UI）指定する仕組みは後続タスクで足す。
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle", meta = (ClampMin = "-1"))
	int64 Seed = -1;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 生成 → 初期散らし → FGame → 表示 → カメラ合わせ（Docs/SPEC_UE.md 8 章 U2 の開始処理） */
	void StartPuzzle();

	/** Seed の決め方（上のコメントの規則）。ランダムに引いたときはログに出す */
	uint32 ResolveSeed();

	/**
	 * プレイヤーの Pawn が軌道カメラなら、注視点と距離をパズルに合わせる。合わせられたら true。
	 * Pawn がまだ湧いていない / BeginPlay 前（合わせても初期値に戻される）なら false を返して再試行に回す
	 */
	bool TryFrameCamera();

	/** Pawn がまだ整っていなかったときの再試行（タイマーから呼ぶ） */
	void RetryFrameCamera();

	/** ピースを描くアクタ。BeginPlay でワールド原点に湧かせる */
	UPROPERTY()
	TObjectPtr<ACubelithPuzzleActor> PuzzleActor;

	/**
	 * ゲーム状態（RULES.md 3.3 / 3.4）。FGame は UObject ではないのでメンバに生で持つ（Docs/SPEC_UE.md 7.1）。
	 * 配置が変わると OnChange から PuzzleActor->UpdatePlacements が呼ばれる
	 */
	TUniquePtr<Cubelith::FGame> Game;

	/** TryFrameCamera の再試行タイマー */
	FTimerHandle FrameCameraTimer;

	/** 再試行した回数 */
	int32 FrameCameraAttempts = 0;
};
