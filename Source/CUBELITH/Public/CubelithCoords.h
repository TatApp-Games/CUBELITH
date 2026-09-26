// ロジックのボクセル座標・向き id を UE のワールド座標・回転へ直す（Docs/SPEC_UE.md 7.2）
// ロジック（CUBELITHCore）側は Web 版と同じ右手系・Y が上・整数座標で持ち、変換は描画のときだけ行う

#pragma once

#include "CoreMinimal.h"

#include "Grid.h"

namespace Cubelith
{
	/** ボクセル 1 マスの大きさ（cm）。UE の標準の立方体と同じ 100 cm（Docs/SPEC_UE.md 7.2） */
	inline constexpr double VoxelSizeCm = 100.0;

	/** ボクセル座標 → UE のワールド座標。UE の (X, Y, Z) = (x, z, y) × VoxelSizeCm（Docs/SPEC_UE.md 7.2） */
	CUBELITH_API FVector VoxelToWorld(const FVec3& Voxel);

	/**
	 * 向き id → UE の回転行列。R_UE = P · R · P（P は y と z を入れ替える置換）。
	 * 平行移動は 0・スケールは 1。FMatrix の規約（行ベクトルを左から掛ける v' = v · M）で詰めてある。
	 */
	CUBELITH_API FMatrix OrientationToWorldMatrix(int32 Orientation);

	/** 向き id → UE の回転クォータニオン（インスタンスの FTransform に渡せる形）。上の行列から作る */
	CUBELITH_API FQuat OrientationToWorldQuat(int32 Orientation);
}
