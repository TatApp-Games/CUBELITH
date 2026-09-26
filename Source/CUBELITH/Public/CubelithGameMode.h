// ゲームの既定の GameMode。既定の Pawn を軌道カメラ（ACubelithOrbitPawn）にし、
// ゲーム開始時にパズルを生成して散らし、ピースを表示して軌道カメラを立方体に合わせる（Docs/SPEC_UE.md 8 章 U2）
// Config/DefaultEngine.ini の GlobalDefaultGameMode がこのクラスを指している

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/GameModeBase.h"
#include "Templates/UniquePtr.h"

#include "CubelithDifficulty.h"
#include "Game.h"

#include "CubelithGameMode.generated.h"

class ACubelithPuzzleActor;

/**
 * 既定の難易度（RULES.md 3.1 の N=3・M=4・パズルの回転なし）でパズルを 1 つ作り、
 * ACubelithPuzzleActor に描かせて軌道カメラを合わせる。
 *
 * 難易度は下の UPROPERTY が既定値で、マップ URL のオプション `?n=` / `?m=` / `?rot=` と
 * コマンドライン引数 `-CubelithN=` / `-CubelithM=` / `-CubelithRotation=` で上書きできる（Docs/SPEC_UE.md 7.7）。
 * 画面での難易度選択・HUD・「次の問題」は U4 で足す。
 * PlayerController は ACubelithPlayerController（ピースの選択。U3）、HUD は既定のまま使う。
 */
UCLASS()
class CUBELITH_API ACubelithGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACubelithGameMode();

	/** クリアの仮表示を出し続けるためだけの Tick（クリアしている間だけ有効になる） */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * ゲーム状態（RULES.md 3.3 / 3.4）。まだ作られていなければ nullptr。
	 * 所有権は GameMode のまま（TUniquePtr で持つ）なので、呼び出し側は寿命を持たない生ポインタとして読む
	 */
	Cubelith::FGame* GetGame() const { return Game.Get(); }

	/** ピースを描くアクタ。まだ湧いていなければ nullptr */
	ACubelithPuzzleActor* GetPuzzleActor() const { return PuzzleActor; }

	/**
	 * この盤面がパズルの回転「あり」か（RULES.md 3.1）。下の bAllowRotation に `?rot=` / `-CubelithRotation=`
	 * を反映して StartPuzzle が決めた値で、ACubelithPlayerController が回転操作の可否に使う。
	 * パズルを開く前は「なし」（そもそも操作する盤面が無い）
	 */
	bool IsRotationAllowed() const { return bResolvedAllowRotation; }

	/**
	 * 空間サイズ N（RULES.md 3.1。既定 3）。
	 * `?n=` / `-CubelithN=` のほうが優先される。範囲外は 3..7 に丸める（Docs/SPEC_UE.md 7.7）
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle", meta = (ClampMin = "3", ClampMax = "7"))
	int32 SpaceSize = 3;

	/**
	 * ピース分割数 M（RULES.md 3.1。N=3 の既定は 4）。
	 * `?m=` / `-CubelithM=` のほうが優先される。N ごとの 5 段のプリセットのうち最も近いものへ寄せる
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle", meta = (ClampMin = "2", ClampMax = "27"))
	int32 PieceCount = 4;

	/**
	 * パズルの回転（RULES.md 3.1。既定は「なし」= 全ピースを恒等の向きで散らす）。
	 * `?rot=` / `-CubelithRotation=` のほうが優先される
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle")
	bool bAllowRotation = false;

	/**
	 * 乱数シード。−1（負の値）= 指定なし、0..4294967295 = その値を uint32 として使う。
	 *
	 * 解釈: RULES.md 3.1 のシードは符号なし 32 bit だが、uint32 は UPROPERTY にできないので int64 で持つ。
	 * マップ URL のオプション `?seed=` とコマンドライン引数 `-CubelithSeed=` のほうが優先される
	 * （Docs/SPEC_UE.md 7.7）。どれも無ければランダムに引き、実際に使った値を LogCubelith に出すので、
	 * 同じパズルを再現したいときはその値をここか `?seed=` に入れる。
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle", meta = (ClampMin = "-1"))
	int64 Seed = -1;

protected:
	/** マップ URL のオプション `?seed=` / `?n=` / `?m=` / `?rot=` を受け取る（Docs/SPEC_UE.md 7.7）。解釈は BeginPlay で行う */
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 生成 → 初期散らし → FGame → 表示 → カメラ合わせ（Docs/SPEC_UE.md 8 章 U2 の開始処理） */
	void StartPuzzle();

	/**
	 * 実際に使うシードを決める（Docs/SPEC_UE.md 7.7 の優先順位）。
	 * コマンドラインとランダムの控えをここで用意し、解釈そのものは Cubelith::ResolveSeed に任せる。
	 * 選んだ値とその経路、無視した不正な指定をログに出す
	 */
	uint32 ResolveSeed();

	/**
	 * 実際に使う難易度（N / M / パズルの回転）を決める（Docs/SPEC_UE.md 7.7 の優先順位）。
	 * コマンドラインをここで読み、解釈そのものは Cubelith::ResolveDifficulty に任せる。
	 * 選んだ値とその経路、無視した指定・寄せた指定をログに出す
	 */
	Cubelith::FDifficultyResolution ResolveDifficulty();

	/**
	 * プレイヤーの Pawn が軌道カメラなら、注視点と距離をパズルに合わせる。合わせられたら true。
	 * Pawn がまだ湧いていない / BeginPlay 前（合わせても初期値に戻される）なら false を返して再試行に回す
	 */
	bool TryFrameCamera();

	/** Pawn がまだ整っていなかったときの再試行（タイマーから呼ぶ） */
	void RetryFrameCamera();

	/**
	 * クリア判定（RULES.md 3.4）の結果を画面とログに反映する。
	 *
	 * 判定そのものは Cubelith::FGame が更新のたびに走らせていて、ここはその結果を受け取るだけ
	 * （判定のロジックは足さない・変えない）。false → true に変わったときに LogCubelith へ 1 行出し、
	 * クリアしている間はずっと見えるよう画面に仮の表示を出す。本実装の UI は U4、演出は U5
	 */
	void UpdateSolvedDisplay(bool bSolved);

	/** クリアの仮表示を出しているか（= 直近に受け取ったクリア判定の結果） */
	bool bSolvedShown = false;

	/**
	 * StartPuzzle が実際に使ったパズルの回転（IsRotationAllowed が返す値）。
	 * UPROPERTY の bAllowRotation は「指定が無ければこれを使う」既定値で、こちらが決まった結果
	 */
	bool bResolvedAllowRotation = false;

	/** ピースを描くアクタ。BeginPlay でワールド原点に湧かせる */
	UPROPERTY()
	TObjectPtr<ACubelithPuzzleActor> PuzzleActor;

	/**
	 * ゲーム状態（RULES.md 3.3 / 3.4）。FGame は UObject ではないのでメンバに生で持つ（Docs/SPEC_UE.md 7.1）。
	 * 配置が変わると OnChange から PuzzleActor->UpdatePlacements が呼ばれる
	 */
	TUniquePtr<Cubelith::FGame> Game;

	/** InitGame で読んだマップ URL のオプション `?seed=` の値。未指定なら空文字 */
	FString SeedOptionText;

	/** InitGame で読んだマップ URL のオプション `?n=` の値。未指定なら空文字 */
	FString SpaceSizeOptionText;

	/** InitGame で読んだマップ URL のオプション `?m=` の値。未指定なら空文字 */
	FString PieceCountOptionText;

	/** InitGame で読んだマップ URL のオプション `?rot=` の値。未指定なら空文字 */
	FString RotationOptionText;

	/** TryFrameCamera の再試行タイマー */
	FTimerHandle FrameCameraTimer;

	/** 再試行した回数 */
	int32 FrameCameraAttempts = 0;
};
