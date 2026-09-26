// スナップの補間移動（RULES.md 3.5。Docs/SPEC_UE.md 8 章 U4）
// 移植元は WebMock/src/render/snapMotion.ts の createSnapMotion。
// 論理上の配置（Cubelith::FPlacement）は整数座標のまま先に確定させ、**表示だけ**を 100〜150 ms かけて
// 追いつかせる。こうするとクリア判定（RULES.md 3.4）も残りピース数も中間位置の影響を受けない。

#pragma once

#include "CoreMinimal.h"

#include "Grid.h"

namespace Cubelith
{
	/** 補間にかける時間（秒）。RULES.md 3.5 の 100〜150 ms の中央付近（TS の SNAP_DURATION_MS = 130） */
	inline constexpr double SnapDurationSeconds = 0.130;

	/** 表示上のずれ 1 件（TS の OffsetSink に流れる 1 回分）。単位はグリッド（1.0 = ボクセル 1 マス） */
	struct FSnapOffsetUpdate
	{
		int32 PieceId = INDEX_NONE;

		/** 表示位置に足すずれ（グリッド単位）。bCleared が true なら意味を持たない */
		FVector Offset = FVector::ZeroVector;

		/** 補間が終わった / 打ち切られた（＝ずれ無しに戻す。TS の setOffset(pieceId, null)） */
		bool bCleared = false;
	};

	/**
	 * スナップの補間を束ねる（TS の SnapMotion）。
	 *
	 * 解釈: 現在時刻は呼び出し側から渡す（TS が nowMs を引数に取るのと同じ。テストで時間を進められる）。
	 * ACubelithPlayerController は UWorld の時刻（GetWorld()->GetTimeSeconds()）を渡す。
	 * ポーズや時間の伸縮の扱いは決めない（U4 では起こらない）。
	 * 単位は TS がミリ秒、こちらは UE に揃えて秒。
	 *
	 * 表示上のずれの配り先は戻り値（OutUpdates への列挙）にした。TFunction を持たせるより
	 * 再入の心配が無く、テストで「その呼び出しで何が起きたか」をそのまま見られる。
	 * OutUpdates は毎回 Reset される（呼び出し側が配列を使い回して確保を避けられる）。
	 */
	class CUBELITH_API FSnapMotion
	{
	public:
		explicit FSnapMotion(double InDurationSeconds = SnapDurationSeconds);

		/**
		 * 表示位置を Offset だけずらした状態から 0 へ補間し始める（TS の start）。
		 * Offset には「移動前の位置 − 移動後の位置」を渡す（見た目が移動前から始まる）。
		 * 同じピースに新しい補間が来たら前の補間は捨てる。Offset が 0 なら即座にずれ無しにする。
		 */
		void Start(int32 PieceId, const FVec3& Offset, double NowSeconds, TArray<FSnapOffsetUpdate>& OutUpdates);

		/** 毎フレーム呼ぶ（TS の update）。走っている補間が無ければ OutUpdates は空になる */
		void Update(double NowSeconds, TArray<FSnapOffsetUpdate>& OutUpdates);

		/** そのピースの補間を打ち切り、表示を論理位置に戻す（TS の cancel）。走っていなければ何もしない */
		void Cancel(int32 PieceId, TArray<FSnapOffsetUpdate>& OutUpdates);

		/** 走っている補間をすべて打ち切る（TS の dispose） */
		void CancelAll(TArray<FSnapOffsetUpdate>& OutUpdates);

		/** 走っている補間の本数 */
		int32 Num() const { return Tweens.Num(); }

		/** そのピースの補間が走っているか */
		bool IsRunning(int32 PieceId) const { return Tweens.Contains(PieceId); }

		/** 補間にかける時間（秒） */
		double DurationSeconds() const { return Duration; }

		/**
		 * 補間にかける時間を差し替える（人がエディタで調整した値を反映するため）。
		 * 走っている補間にもそのまま効く（次の Update から新しい時間で進む）。0 以下なら即座に終わる
		 */
		void SetDurationSeconds(double InDurationSeconds) { Duration = InDurationSeconds; }

	private:
		/** 進行中の 1 本（TS の Tween）。ずれは整数（開始時の「移動前 − 移動後」）なので FVec3 で持つ */
		struct FTween
		{
			FVec3 Offset;
			double StartSeconds = 0.0;
		};

		/** その補間を捨ててずれ無しを積む。走っていなければ何もしない（TS の clear） */
		void Clear(int32 PieceId, TArray<FSnapOffsetUpdate>& OutUpdates);

		/**
		 * ピース id → 進行中の補間（TS の tweens）。id 引きしかせず、列挙の順序は
		 * 「どのピースの表示をずらすか」に影響しないので TMap でよい（Docs/SPEC_UE.md 7.1）
		 */
		TMap<int32, FTween> Tweens;

		/** 補間にかける時間（秒） */
		double Duration = SnapDurationSeconds;
	};

	/** 終わり際が緩む easing（TS の easeOutCubic）。吸い付きが「シュッ」と減速して止まる */
	CUBELITH_API double EaseOutCubic(double T);
}
