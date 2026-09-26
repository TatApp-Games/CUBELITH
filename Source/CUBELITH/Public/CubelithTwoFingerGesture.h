// 2 本指ジェスチャの認識（Docs/RULES.md 3.3 / 6 章）。移植元は WebMock/src/input/twoFingerGesture.ts。
// エンジンの入力の型に依存しない純粋な状態機械で、呼び出し側が 2 本の指の画面座標を渡すと
// 「ピンチ」か「90 度回転」かを返す。
//
// 割り当て（解釈・移植元のまま）: RULES.md 3.3 は回転も奥行き移動も「2 本指スワイプ **または** UI ボタン」を
// 認めている。両方を 2 本指に載せると区別できないので、**2 本指は回転とピンチズームに割り当て、
// 奥行き方向の専用操作は持たない**（カメラを回してその軸を画面上に出してからドラッグで動かす）。
//
// 回転は「閾値を超えたら 1 回だけ」。一度出したら指を離すまで次を出さない（Reset で解除）。
// こうしないとスワイプし続ける間ピースが回り続けてしまう。

#pragma once

#include "CoreMinimal.h"

namespace Cubelith
{
	/** ピンチと判定する指間距離の変化率（開始時の距離に対する比） */
	inline constexpr double PinchRatioThreshold = 0.14;

	/** ひねり（Roll）と判定する 2 本指を結ぶ線の回転角（ラジアン。約 26 度） */
	inline constexpr double TwistRadiansThreshold = 0.45;

	/** 平行スワイプ（Yaw / Pitch）と判定する 2 本指の中点の移動量（px） */
	inline constexpr double SwipePixelsThreshold = 44.0;

	/** 認識した操作の種類（TS の TwoFingerAction の kind） */
	enum class ETwoFingerActionKind : uint8
	{
		/** 何も起きていない */
		None,
		/** ピンチ */
		Zoom,
		/** 90 度回転を 1 回 */
		Rotate,
	};

	/**
	 * 90 度回転の種類（TS の gesture）。
	 * - Yaw: 画面の上方向を軸に回す。Dir = +1 が「2 本指を右へ」
	 * - Pitch: 画面の右方向を軸に回す。Dir = +1 が「2 本指を上へ」
	 * - Roll: 画面の奥方向を軸に回す。Dir = +1 が「画面上で時計回りにひねる」
	 */
	enum class ETwoFingerRotateGesture : uint8
	{
		Yaw,
		Pitch,
		Roll,
	};

	/** 認識した操作（TS の TwoFingerAction。判別共用体をフラットな構造体にした） */
	struct CUBELITH_API FTwoFingerAction
	{
		ETwoFingerActionKind Kind = ETwoFingerActionKind::None;

		/** Kind == Zoom のときだけ意味を持つ。カメラ半径に掛ける倍率（< 1 で寄る） */
		double Scale = 1.0;

		/** Kind == Rotate のときだけ意味を持つ */
		ETwoFingerRotateGesture Gesture = ETwoFingerRotateGesture::Yaw;

		/** Kind == Rotate のときだけ意味を持つ。+1 または -1 */
		int32 Dir = 0;
	};

	/**
	 * 2 本指の状態機械（TS の createTwoFingerGesture が返すクロージャをクラスにした。Docs/SPEC_UE.md 7.1）。
	 *
	 * 最初に閾値を超えた種類でモードを固定する（ピンチ中に回らない / 回した直後に寄らない）。
	 * 三者は「閾値に対する進み具合」で比べるので、単位の違う量でも公平に競わせられる。
	 */
	class CUBELITH_API FTwoFingerGesture
	{
	public:
		/** 2 本目の指が触れた / 指の組が変わったときに基準を取り直す */
		void Reset(const FVector2D& A, const FVector2D& B);

		/** 指が動くたびに呼ぶ。認識した操作を返す（何も無ければ Kind == None） */
		FTwoFingerAction Update(const FVector2D& A, const FVector2D& B);

	private:
		/** Idle = まだ種類が決まっていない、Zoom = ピンチ継続、Done = 回転を出し終えて打ち止め */
		enum class EMode : uint8
		{
			Idle,
			Zoom,
			Done,
		};

		/** 基準（開始時の距離・角度・中点）を取り直す（TS の anchor） */
		void Anchor(const FVector2D& A, const FVector2D& B);

		EMode Mode = EMode::Idle;
		double StartDistance = 0.0;
		double StartAngle = 0.0;
		double StartCenterX = 0.0;
		double StartCenterY = 0.0;

		/** ピンチの倍率は「前フレームからの変化」で出すので直近の距離を持つ */
		double LastDistance = 0.0;
	};

	/**
	 * ラジアンを畳んでひねりの巻き戻りを消す（TS の normalizeAngle）。テストから確かめるので公開する。
	 * 解釈: 移植元のコメントは (-PI, PI] と書いているが、式（剰余のあと PI を引く）では PI が -PI に落ちるので
	 * 実際の範囲は [-PI, PI)。挙動は WebMock の実装が正（Source/CLAUDE.md 開発ルール 2）なので式のまま写す。
	 */
	CUBELITH_API double NormalizeAngle(double Radians);
}
