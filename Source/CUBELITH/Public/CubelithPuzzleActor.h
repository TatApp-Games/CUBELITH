// ピース群を画面に出すアクタ。1 ピース = 1 UInstancedStaticMeshComponent（Docs/SPEC_UE.md 4 章・8 章 U2）
// 移植元は WebMock/src/render/pieces.ts。1 ピース 1 InstancedMesh・ボクセルをわずかに縮める・
// ピースごとに色相をずらす・解答空間の中心を原点に合わせる、という組み立てをそのまま写してある
// マテリアルは仮（U2 の段階。すりガラスと発光コアは U5 で人が作る）

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "GameFramework/Actor.h"

#include "Piece.h"

#include "CubelithPuzzleActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;

/**
 * 生成したピースをボクセルの立方体で描くアクタ。ワールド原点に湧かせて使う。
 *
 * ピースごとに UInstancedStaticMeshComponent を 1 つ持ち（ドローコールがピース数で収まる。原典 4.1）、
 * 配置が変わったら UpdatePlacements でインスタンスの平行移動だけを書き直す。
 * 見た目に関わる値（メッシュ・マテリアル・色・ボクセルの詰まり具合）は UPROPERTY(EditAnywhere) で
 * 公開してあり、人がエディタで差し替えられる（Docs/SPEC_UE.md 0 章）。
 */
UCLASS()
class CUBELITH_API ACubelithPuzzleActor : public AActor
{
	GENERATED_BODY()

public:
	ACubelithPuzzleActor();

	/**
	 * ピースごとの UInstancedStaticMeshComponent と色を用意する。N は解答空間のサイズ（中心合わせに使う）。
	 * インスタンスはまだ置かないので、続けて UpdatePlacements を呼ぶこと。
	 */
	void Build(TArrayView<const Cubelith::FPiece> Pieces, int32 N);

	/**
	 * 配置を反映する。Placements は全ピース分でも一部でもよく、来なかったピースは前の位置に留まる
	 * （FGame の OnChange は全ピース分を渡してくるが、動いたピースだけを書き直しても破綻しない形にしてある）。
	 */
	void UpdatePlacements(TArrayView<const Cubelith::FPlacement> Placements);

	/**
	 * 直近の UpdatePlacements で置いた全ボクセルを含む球の半径（cm）。アクタの原点（= 解答空間の中心）から
	 * 最も遠いボクセルの中心までの距離 + ボクセル 1 個分。軌道カメラの距離合わせ（FrameSphere）に使う。
	 * まだ何も置いていなければ 0。
	 */
	double GetBoundingRadiusCm() const { return BoundingRadiusCm; }

	/** ボクセル 1 個のメッシュ。既定はエンジンの立方体 /Engine/BasicShapes/Cube（1 辺 100 cm） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Render")
	TObjectPtr<UStaticMesh> VoxelMesh;

	/**
	 * ボクセルのマテリアル。既定はエンジンの /Engine/BasicShapes/BasicShapeMaterial。
	 * ピースごとに UMaterialInstanceDynamic を作って ColorParameterName に色を流し込む。
	 * すりガラス（原典 4.2）に差し替えるのは U5 で人が行う
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Render")
	TObjectPtr<UMaterialInterface> VoxelMaterial;

	/** ピース色を流し込むベクトルパラメータの名前。マテリアルに無いと色が付かない（Build で警告を出す） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Render")
	FName ColorParameterName = TEXT("Color");

	/**
	 * ボクセル 1 個の大きさの、1 マスに対する比。隣り合うボクセルの境目が見えてピースの形が分かるよう
	 * わずかに縮める（pieces.ts の VOXEL_SIZE = 0.96 と同じ）
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Render", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float VoxelFillRatio = 0.96f;

	/**
	 * ピース色の色相をずらす量（0..1）。ピース index / ピース数 にこれを足して 1 で折り返す
	 * （pieces.ts の pieceColor の + 0.55）。0 番のピースの色を変えたいときに動かす
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Render", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ColorHueOffset = 0.55f;

	/** ピース色の彩度（pieces.ts の HSL の S = 0.62。下の解釈のとおり UE は HSV なので同じ色にはならない） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Render", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ColorSaturation = 0.62f;

	/** ピース色の明度（pieces.ts の HSL の L = 0.58 に当たる値。UE では HSV の V として使う） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Render", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ColorValue = 0.58f;

private:
	/** ピース index からピース色を作る（pieces.ts の pieceColor） */
	FLinearColor MakePieceColor(int32 Index, int32 Count) const;

	/** メッシュ 1 個の 1 辺の長さ（cm）。インスタンスのスケールをここから決める */
	double GetVoxelMeshSizeCm() const;

	/** ピース id からピースを引く。未知の id なら nullptr */
	const Cubelith::FPiece* FindPiece(int32 PieceId) const;

	/** ルート。このアクタの位置が解答空間の中心になる（= 軌道カメラの注視点） */
	UPROPERTY(VisibleAnywhere, Category = "Cubelith|Render")
	TObjectPtr<USceneComponent> PuzzleRoot;

	/** ピース id → そのピースのインスタンス群。U3 で選択したピースを引くのにも使う */
	UPROPERTY()
	TMap<int32, TObjectPtr<UInstancedStaticMeshComponent>> PieceMeshes;

	/** Build で受け取ったピース（局所座標）。UpdatePlacements で PlacedVoxels に渡す */
	TArray<Cubelith::FPiece> PieceList;

	/** ピース id → PieceList の添字 */
	TMap<int32, int32> PieceIndexById;

	/** 解答空間のサイズ N。中心合わせのオフセットに使う */
	int32 SpaceSize = 0;

	/** GetBoundingRadiusCm が返す値 */
	double BoundingRadiusCm = 0.0;

	/**
	 * Build を呼んだ回数 - 1。2 回目以降のコンポーネント名に付けて、前の Build で作った
	 * コンポーネント（GC 待ちで名前が残っていることがある）と衝突しないようにする
	 */
	int32 BuildGeneration = 0;
};
