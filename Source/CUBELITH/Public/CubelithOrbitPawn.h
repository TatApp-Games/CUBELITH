// 注視点まわりを旋回・ズームする軌道カメラの Pawn（RULES.md 3.3「カメラ」・Docs/SPEC_UE.md 4 章）
// 移植元は WebMock/src/render/camera.ts。旋回・ズーム・減衰・可動域の考え方と既定値はそこに合わせてある

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "CubelithOrbitPawn.generated.h"

class UCameraComponent;
class USceneComponent;
class USpringArmComponent;

/**
 * 注視点（このアクタの位置）のまわりを球面座標（方位角・仰角・距離）で回るカメラ。
 * ドラッグで旋回、ホイール / ピンチでズームする。camera.ts と同じく「目標値」と「現在値」を分け、
 * Tick で目標値へ減衰しながら追従させることで慣性を出す。
 * 旋回は bOrbitEnabled で止められるが、ズームは止まらない（RULES.md 3.3。選択の有無によらず効かせる）。
 *
 * 入力は Enhanced Input の InputMappingContext / InputAction（= `.uasset`）を使わず、
 * Tick で APlayerController から生の入力状態をポーリングして読む。アセットを作らないため（Docs/SPEC_UE.md 0 章）。
 */
UCLASS()
class CUBELITH_API ACubelithOrbitPawn : public APawn
{
	GENERATED_BODY()

public:
	ACubelithOrbitPawn();

	virtual void Tick(float DeltaSeconds) override;

	/** 注視点（このアクタの位置）を置き直す */
	void SetOrbitTarget(const FVector& WorldTarget);

	/** 半径 BoundingRadiusCm の球が画面に収まる距離へ引く（camera.ts の frame）。現在値にも即時反映する */
	void FrameSphere(double BoundingRadiusCm);

	/**
	 * ドラッグでの**旋回**の有効・無効（camera.ts の enabled のうち旋回の部分）。
	 *
	 * ピースを選択している間は ACubelithPlayerController がここを false にして、ドラッグをピース操作に回す。
	 * 何も無い場所から始めたドラッグの間だけは、選択中でも true に戻る（RULES.md 3.3「カメラ」）。
	 *
	 * **ズームはこれでは止まらない**。ホイール / ピンチはピースの選択の有無によらず常に効く（RULES.md 3.3）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cubelith|Camera")
	bool bOrbitEnabled = true;

	/** ドラッグ 1 px あたりの回転量（度）。camera.ts の ROTATE_SPEED = 0.006 rad/px 相当 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera", meta = (ClampMin = "0.0"))
	double RotateSensitivityDegPerPixel = 0.34;

	/**
	 * ホイール 1 ノッチあたりのズーム倍率（1 より大きい値）。手前に転がすと寄り、奥に転がすと引く。
	 * camera.ts は exp(deltaY * 0.0015) で、ブラウザの 1 ノッチが deltaY ≒ 100 なので倍率 ≒ 1.16。
	 * UE のホイール軸は 1 ノッチで ±1 なので、この値をそのまま 1 ノッチの倍率として持つ。
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera", meta = (ClampMin = "1.0"))
	double WheelZoomScalePerNotch = 1.16;

	/** ピンチの感度。1 で 2 本指の間隔の比そのまま（camera.ts と同じ）、大きくすると強く効く */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera", meta = (ClampMin = "0.0"))
	double PinchZoomSensitivity = 1.0;

	/** 注視点からカメラまでの距離の下限（cm）。camera.ts の minRadius = 2 ボクセル相当 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera", meta = (ClampMin = "1.0"))
	double MinDistanceCm = 200.0;

	/** 注視点からカメラまでの距離の上限（cm）。camera.ts の maxRadius = 200 ボクセル相当 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera", meta = (ClampMin = "1.0"))
	double MaxDistanceCm = 20000.0;

	/**
	 * 初期の方位角（度）。UE のヨー角で持つ。
	 * camera.ts の azimuth = PI * 0.25 は、7.2 の y と z の入れ替えを通すと UE のヨー -135 度に当たる。
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera")
	double InitialAzimuthDeg = -135.0;

	/** 初期の仰角（度。正でカメラが注視点より上）。camera.ts の polar = PI * 0.38（= 仰角 21.6 度）相当 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera")
	double InitialElevationDeg = 21.6;

	/** 初期の距離（cm）。camera.ts の radius = 20 ボクセル相当。003 でパズルの大きさに合わせて FrameSphere で上書きする */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera", meta = (ClampMin = "1.0"))
	double InitialDistanceCm = 2000.0;

	/** 仰角の可動域（度）。真上・真下で姿勢が壊れないよう手前で止める。camera.ts の 90 度 - 4.6 度相当 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera", meta = (ClampMin = "0.0", ClampMax = "89.9"))
	double MaxElevationDeg = 85.4;

	/**
	 * 目標値へ追従する速さ（1/秒）。大きいほど機敏になり、0 で即時。
	 * camera.ts の DAMPING = 0.18（60 fps の 1 フレームあたりの割合）は約 11.9 に当たる。
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera", meta = (ClampMin = "0.0"))
	double DampingRate = 11.9;

	/** カメラの視野角（度）。camera.ts の PerspectiveCamera の fov = 50 と同じ */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera", meta = (ClampMin = "1.0", ClampMax = "170.0"))
	double FieldOfViewDeg = 50.0;

	/** FrameSphere で画面に残す余裕の倍率。camera.ts の 1.15 と同じ */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Camera", meta = (ClampMin = "1.0"))
	double FrameMarginScale = 1.15;

protected:
	virtual void BeginPlay() override;

private:
	/** 注視点。ルートに置き、このアクタの位置がそのまま注視点になる */
	UPROPERTY(VisibleAnywhere, Category = "Cubelith|Camera")
	TObjectPtr<USceneComponent> OrbitRoot;

	/** 腕の長さがカメラまでの距離。当たり判定と減衰はどちらも自前で持つので切る */
	UPROPERTY(VisibleAnywhere, Category = "Cubelith|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Cubelith|Camera")
	TObjectPtr<UCameraComponent> CameraComponent;

	/** 目標値と現在値を初期値（Initial*）から作り直す */
	void ResetToInitial();

	/** APlayerController から生の入力状態を読み、目標値を動かす */
	void PollInput();

	/** 画面上の移動量（px。Y は下向きが正）を方位角・仰角の目標値に足す */
	void AddOrbitDelta(double ScreenDeltaX, double ScreenDeltaY);

	/** 距離の目標値に倍率を掛ける（1 より小さい値で寄る） */
	void MultiplyTargetDistance(double Scale);

	/** 距離の目標値を下限・上限で丸めて置く */
	void SetTargetDistance(double NewDistanceCm);

	/** 現在値を目標値へ減衰しながら近づける */
	void AdvanceTowardTarget(double DeltaSeconds);

	/** 現在値をスプリングアームに反映する */
	void ApplyToComponents();

	/** 球を画面に収める判定に使う、画角の狭い側の半角（ラジアン） */
	double GetFitHalfFovRadians() const;

	// 目標値（入力で動かす）と現在値（描画に使う）。差を減衰で詰めることで慣性が出る（camera.ts と同じ）
	double TargetAzimuthDeg = 0.0;
	double TargetElevationDeg = 0.0;
	double TargetDistanceCm = 0.0;
	double CurrentAzimuthDeg = 0.0;
	double CurrentElevationDeg = 0.0;
	double CurrentDistanceCm = 0.0;

	// タッチの追跡。前のフレームの状態と比べて差分を取る（ポーリングなので押した瞬間のイベントが無い）
	FVector2D PreviousTouch1 = FVector2D::ZeroVector;
	bool bWasTouch1Down = false;
	bool bWasTouch2Down = false;
	double PreviousPinchDistance = 0.0;
};
