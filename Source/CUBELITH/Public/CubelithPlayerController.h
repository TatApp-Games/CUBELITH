// クリック / タップでピースを選ぶ PlayerController（RULES.md 3.3「ピース選択」・Docs/SPEC_UE.md 8 章 U3）
// 移植元は WebMock/src/input/pieceInput.ts の pickPiece / setSelected。
// 押した瞬間にライントレースでピースを引き、当たったピースを選択中にする。
// 何も無い場所を押しても選択は外さない（RULES.md 3.3。外れるのは「散らし直す」など外からの解除だけで、それは U4）。
//
// ドラッグでの移動・カメラとの切り分けは 003、90 度回転と 2 本指ジェスチャは 005。
// このタスクの時点では、選択中のピースをドラッグするとカメラ（ACubelithOrbitPawn）も回る。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "CubelithPickSamples.h"

#include "CubelithPlayerController.generated.h"

class ACubelithGameMode;
class ACubelithPuzzleActor;

/**
 * ピースの選択を受け持つ PlayerController。ACubelithGameMode がコンストラクタで PlayerControllerClass に指定する。
 *
 * 入力は Enhanced Input の InputAction / InputMappingContext（= `.uasset`）を使わず、PlayerTick で
 * APlayerController から生の入力状態をポーリングして読む（Docs/SPEC_UE.md 0 章・4 章。
 * ACubelithOrbitPawn::PollInput と同じやり方）。ポーリングなので「押した瞬間」のイベントが無く、
 * 前フレームの押下状態と比べて立ち上がりを自分で見る。
 */
UCLASS()
class CUBELITH_API ACubelithPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACubelithPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	/** 選択中のピース id。未選択は INDEX_NONE */
	int32 GetSelectedPieceId() const { return SelectedPieceId; }

	/**
	 * 選択を置き換える（INDEX_NONE で解除）。見た目への反映は ACubelithPuzzleActor::SetSelectedPiece に任せる。
	 * 外からの解除（「散らし直す」など。RULES.md 3.3）は U4 でここを呼ぶ
	 */
	void SetSelectedPiece(int32 PieceId);

	/**
	 * タッチの近接ピックの許容半径（CSS ピクセル相当。pieceInput.ts の TOUCH_PICK_RADIUS_PX）。
	 * 指の接触面は広く狙いも粗いのでマウスより広く取る
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "0.0"))
	double TouchPickRadiusPx = 16.0;

	/** マウスの近接ピックの許容半径（CSS ピクセル相当。pieceInput.ts の PRECISE_PICK_RADIUS_PX） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "0.0"))
	double MousePickRadiusPx = 8.0;

	/**
	 * 近接ピックの半径を画面の DPI スケールで割り増すか。
	 *
	 * 解釈: 上の半径はブラウザの CSS ピクセル（= 端末非依存のピクセル）で決めた値だが、
	 * DeprojectScreenPositionToWorld とタッチ座標が使うのはビューポートのピクセル（GetViewportSize の単位。
	 * 高 DPI の端末では 1 CSS ピクセルが複数ピクセルに当たる）。そこで UMG と同じ DPI スケール
	 * （UUserInterfaceSettings::GetDPIScaleBasedOnSize）を掛けて、指の太さに対する当たりの広さを端末間で揃える。
	 * 厳密な一致は求めない（手触りの調整は人が半径そのものを動かして行う）
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input")
	bool bScalePickRadiusByDpi = true;

	/** ピック用のライントレースを飛ばす長さ（cm）。カメラの遠クリップより十分長ければよい */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "1.0"))
	double PickTraceDistanceCm = 1000000.0;

protected:
	virtual void BeginPlay() override;

private:
	/** マウスの左ボタンとタッチの押下の立ち上がりを見て、1 回だけピックを走らせる */
	void PollPointerPress();

	/** 押した瞬間の処理。ピースに当たったときだけ選択を置き換える */
	void HandlePointerPressed(const FVector2D& ScreenPosition, bool bTouch);

	/**
	 * 画面座標のピースを拾う。何も無ければ INDEX_NONE（pieceInput.ts の pickPiece）。
	 * 中心のレイが当たればそれを採り、外れたときだけ近接サンプルを撃ってレイ原点に最も近いヒットを選ぶ
	 */
	int32 PickPieceAtScreenPosition(const FVector2D& ScreenPosition, bool bTouch);

	/**
	 * レイ 1 本を撃って最も手前のピースを返す。当たらなければ INDEX_NONE。
	 * ピース以外の物（床など）に遮られても、その後ろのピースを拾えるよう複数ヒットで撃つ
	 * （pieceInput.ts のレイキャストがピースだけを対象にしているのに合わせる）
	 */
	int32 TraceNearestPiece(const FVector& WorldOrigin, const FVector& WorldDirection, double& OutDistance) const;

	/** 近接ピックのサンプル点を（必要なら作り直して）用意する */
	void EnsurePickOffsets();

	/** CSS ピクセル → ビューポートのピクセルの倍率（bScalePickRadiusByDpi が false なら 1） */
	double GetPickRadiusScale() const;

	ACubelithGameMode* GetCubelithGameMode() const;
	ACubelithPuzzleActor* GetPuzzleActor() const;

	/**
	 * 近接ピックのサンプル点（Cubelith::PickSampleOffsets）。半径ごとに固定なので BeginPlay で 1 度作って
	 * 使い回す。DPI スケールが変わったとき（ウィンドウを別の画面へ動かした等）だけ作り直す
	 */
	TArray<Cubelith::FPickOffset> TouchPickOffsets;
	TArray<Cubelith::FPickOffset> MousePickOffsets;

	/** 上のサンプル点を作ったときの倍率。負なら未作成 */
	double PickOffsetsScale = -1.0;

	/** 前フレームの押下状態（ポーリングなので立ち上がりを自分で見る） */
	bool bWasTouchDown = false;
	bool bWasMouseDown = false;

	/** 選択中のピース id（未選択は INDEX_NONE） */
	int32 SelectedPieceId = INDEX_NONE;
};
