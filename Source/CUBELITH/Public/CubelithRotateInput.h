// 回転の操作をロジックの言葉へ翻訳する純粋関数（Docs/RULES.md 3.3「回転」）。
// 移植元は WebMock/src/input/pieceInput.ts の applyTwoFinger（2 本指 → グリッド軸まわりの 90 度）と
// updateRotateDrag のギズモなしの経路（回転モードのドラッグ → カメラ基底まわりのトラックボール回転）。
//
// CubelithAxisMapping.h・CubelithFreeRotation.h と同じ「画面 / カメラの都合をロジックの言葉へ翻訳する」層で、
// エンジンの入力やカメラの型には依存しない（呼び出し側がジェスチャの結果とカメラの基底を取り出して渡す）。
// テストは Source/CUBELITH/Private/Tests/RotateInputTest.cpp。

#pragma once

#include "CoreMinimal.h"

#include "CubelithAxisMapping.h"
#include "CubelithTwoFingerGesture.h"
#include "Grid.h"

namespace Cubelith
{
	/**
	 * 回転モードのドラッグ感度（ドラッグ 1 px あたりの回転角・度）。TS の ROTATE_DEGREES_PER_PIXEL。
	 * 90 度回すのに 225 px なので、スマホの短辺（〜390 px）の中で 1 回転の 1/4 を無理なく越えられ、
	 * かつ指の震えでスナップ先が変わらない程度に鈍い。
	 */
	inline constexpr double RotateDegreesPerPixel = 0.4;

	/** グリッド軸まわりの 90 度回転 1 回分（TS の onRotate の引数）。Dir が 0 なら「回転ではない」 */
	struct CUBELITH_API FRotateStep
	{
		/** 回すグリッド軸（ロジック座標の x / y / z） */
		EAxis Axis = EAxis::X;

		/** +1 / -1。0 は回転ではない（ジェスチャがピンチだった / 何も起きていない） */
		int32 Dir = 0;
	};

	/**
	 * 2 本指ジェスチャを、グリッド軸まわりの 90 度回転へ写す（TS の applyTwoFinger の軸と向きの決め方）。
	 *
	 * 軸は画面基準（Yaw = 画面の上、Pitch = 画面の右、Roll = 画面の奥）で決め、Axes でグリッド軸へ写す。
	 * 向きは「見たまま回る」ように取る:
	 * 右へスワイプ → 手前の面が右へ（画面の上軸まわりに +90 度）、
	 * 上へスワイプ → 上の面が奥へ（画面の右軸まわりに −90 度）、
	 * 時計回りにひねる → 画面上でも時計回り（画面の奥軸まわりに +90 度）。
	 *
	 * Action.Kind が Rotate 以外なら Dir = 0 を返す（呼び出し側はそれを見て何もしない）。
	 */
	CUBELITH_API FRotateStep TwoFingerRotateStep(const FTwoFingerAction& Action, const FDragAxes& Axes);

	/**
	 * 回転モードのドラッグ量から、カメラ基底まわりのトラックボール回転を作る
	 * （TS の updateRotateDrag のギズモなしの経路）。90 度には縛らない見せ方用。
	 *
	 * 向きの割り当ては TwoFingerRotateStep と同じ「見たまま回る」考え方:
	 * 右へドラッグ → カメラの上ベクトルまわりに +、上へドラッグ → カメラの右ベクトルまわりに −。
	 *
	 * 引数の基底は **UE ワールド**（Z が上・左手系）のベクトルで、戻り値も UE ワールドの回転
	 * （ACubelithPuzzleActor::SetFreeRotation と Cubelith::SnappedOrientation がどちらも受け取る形）。
	 * ScreenDelta は開始位置からの画面上の移動量（px。Y は下向きが正）で、毎回 0 からの総量を渡す
	 * （差分を積み上げると誤差が溜まるため。TS も base から作り直している）。
	 * 基底が縮退していれば恒等を返す。
	 */
	CUBELITH_API FQuat TrackballRotation(
		const FVector& WorldCameraRight,
		const FVector& WorldCameraUp,
		double ScreenDeltaX,
		double ScreenDeltaY,
		double DegreesPerPixel = RotateDegreesPerPixel);
}
