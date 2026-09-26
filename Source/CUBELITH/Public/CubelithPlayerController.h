// ピースの選択とドラッグ移動を受け持つ PlayerController（RULES.md 3.3・Docs/SPEC_UE.md 8 章 U3）
// 移植元は WebMock/src/input/pieceInput.ts の pickPiece / setSelected / MoveDrag。
// 押した瞬間にライントレースでピースを引き、当たったピースを選択中にする。
// 何も無い場所を押しても選択は外さない（RULES.md 3.3。外れるのは「散らし直す」など外からの解除だけで、それは U4）。
//
// ドラッグは押した瞬間に役割が決まり、離すまで変わらない:
//   ピースを押した  → そのピースをカメラの向きに応じた 2 軸へボクセル単位で動かす（軌道カメラの旋回は止める）
//   何も無い場所    → 選択は保ったままカメラを旋回させる（ACubelithOrbitPawn::bOrbitEnabled を戻す）
// ホイール / ピンチのズームは選択の有無によらず効く（ACubelithOrbitPawn が受け持つ）。
//
// スナップは U4、90 度回転と 2 本指ジェスチャは 005（ここでは指が 2 本になったら移動を打ち切るだけ）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "CubelithAxisMapping.h"
#include "CubelithPickSamples.h"

#include "CubelithPlayerController.generated.h"

class ACubelithGameMode;
class ACubelithOrbitPawn;
class ACubelithPuzzleActor;

namespace Cubelith
{
	class FGame;
}

/**
 * ドラッグ 1 本分の役割。押した瞬間に決めて、指 / ボタンを離すまで変えない（RULES.md 3.3）。
 * UENUM にはしない（Blueprint へ出す必要が無く、.uasset も作らないため）。
 */
enum class ECubelithDragMode : uint8
{
	/** ドラッグしていない */
	None,

	/** 押したピースをボクセル単位で動かしている */
	MovePiece,

	/** 何も無い場所から始めたドラッグ。カメラの旋回に回す（選択は保つ） */
	OrbitCamera,
};

/**
 * ピースの選択とドラッグ移動を受け持つ PlayerController。ACubelithGameMode がコンストラクタで
 * PlayerControllerClass に指定する。
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

	/**
	 * 1 マス動かすのに必要なドラッグ量（px）の下限（pieceInput.ts の MIN_PIXELS_PER_VOXEL）。
	 * 基準は「ピースの奥行きでの 1 ボクセルの画面上の大きさ」なので、画面サイズやカメラ距離が変わっても
	 * 「ボクセル 1 個分ドラッグすれば 1 マス動く」体感になる。極端なズームで感度が壊れないよう両端で丸める
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "1.0"))
	double MinPixelsPerVoxel = 14.0;

	/** 同じくドラッグ量（px）の上限（pieceInput.ts の MAX_PIXELS_PER_VOXEL） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "1.0"))
	double MaxPixelsPerVoxel = 160.0;

protected:
	virtual void BeginPlay() override;

private:
	/** マウスとタッチの押下・移動・解放をポーリングで拾い、押下の立ち上がり / 立ち下がりを自分で見る */
	void PollPointer();

	/** 押した瞬間の処理。ピースに当たれば選択してドラッグ移動を始め、外れればカメラの旋回に回す */
	void HandlePointerPressed(const FVector2D& ScreenPosition, bool bTouch);

	/**
	 * 押したまま動かしたときの処理。ドラッグ移動の最中だけ、開始位置からの画面上の移動量を
	 * マス数に直して Cubelith::FGame::Move へ流す（pieceInput.ts の onPointerMove の移動の部分）
	 */
	void HandlePointerMoved(const FVector2D& ScreenPosition);

	/** 離したときの処理。スナップ（U4）はここに入る。今はドラッグの役割を畳むだけ */
	void HandlePointerReleased();

	/**
	 * ドラッグ移動を始める。開始画面座標・ドラッグ軸・感度をこの時点で固定するので、
	 * 途中でカメラが動いても写像や 1 マスの長さがぶれない（pieceInput.ts の MoveDrag）
	 */
	void BeginMoveDrag(int32 PieceId, const FVector2D& ScreenPosition);

	/** ドラッグを畳み、軌道カメラの旋回を「選択中は止める」状態へ戻す（選択は保つ） */
	void EndDrag();

	/** プレイヤーのカメラの基底（UE のワールド座標）。取れなければ false */
	bool GetCameraBasis(FVector& OutRight, FVector& OutUp, FVector& OutForward) const;

	/** 今のカメラからドラッグ軸の割り当てを作る（pieceInput.ts の currentAxes）。取れなければ false */
	bool ComputeDragAxes(Cubelith::FDragAxes& OutAxes) const;

	/**
	 * ピースの奥行きでの「1 ボクセル = 何 px」（pieceInput.ts の pixelsPerVoxelAt）。
	 * MinPixelsPerVoxel 〜 MaxPixelsPerVoxel で丸めた値を返す
	 */
	double PixelsPerVoxelAt(int32 PieceId) const;

	/** 固定中（RULES.md 3.3「固定」）のピースか。ゲーム状態が無ければ false */
	bool IsPieceLocked(int32 PieceId) const;

	/** 軌道カメラの旋回の有効・無効を、今の選択とドラッグの役割から決め直す */
	void UpdateOrbitEnabled();

	/** ゲーム状態。まだ作られていなければ nullptr */
	Cubelith::FGame* GetGame() const;

	/** 操作しているプレイヤーの軌道カメラ。軌道カメラでなければ nullptr */
	ACubelithOrbitPawn* GetOrbitPawn() const;

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

	/** 今のドラッグの役割。押した瞬間に決まり、離すまで変わらない */
	ECubelithDragMode DragMode = ECubelithDragMode::None;

	/** ドラッグ移動しているピース id（していなければ INDEX_NONE） */
	int32 DragPieceId = INDEX_NONE;

	/** ドラッグを始めた画面座標（Y は下向きが正）。移動量はここからの差で測る */
	FVector2D DragStartScreenPosition = FVector2D::ZeroVector;

	/** 開始時に固定したドラッグ軸（画面の右 / 上がどのグリッド軸に当たるか） */
	Cubelith::FDragAxes ActiveDragAxes;

	/** 開始時に固定した感度（1 ボクセル = 何 px） */
	double DragPixelsPerVoxel = 1.0;

	/** これまでに Cubelith::FGame::Move へ流したマス数。差分だけを流すために持つ */
	int32 DragAppliedRight = 0;
	int32 DragAppliedUp = 0;
};
