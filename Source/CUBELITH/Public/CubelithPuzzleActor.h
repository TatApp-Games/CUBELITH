// ピース群を画面に出すアクタ。1 ピース = 1 UInstancedStaticMeshComponent（Docs/SPEC_UE.md 4 章・8 章 U2）
// 移植元は WebMock/src/render/pieces.ts。1 ピース 1 InstancedMesh・ボクセルをわずかに縮める・
// ピースごとに色相をずらす・解答空間の中心を原点に合わせる、という組み立てをそのまま写してある
// マテリアルは仮（U2 の段階。すりガラスと発光コアは U5 で人が作る）
// U3 で選択の強調（SetSelectedPiece）と、回転中の 90 度に縛らない見せ方（SetFreeRotation）を足した
// U4 でスナップ候補の仮の発光（SetSnapHint）と、表示だけのずれ（SetViewOffset / ClearViewOffset）を足した
// U4 で固定の鍵アイコンの仮表示（SetLockIcon。RULES.md 6 章）も足した（銀 = 手動の固定・金 = ヒント）

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "GameFramework/Actor.h"
#include "Misc/Optional.h"

#include "Game.h"
#include "Piece.h"

#include "CubelithPuzzleActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
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

	/**
	 * ライントレースのヒットしたコンポーネントからピース id を引く。ピースのものでなければ INDEX_NONE。
	 * ACubelithPlayerController のピックが使う（RULES.md 3.3「ピース選択」）
	 */
	int32 FindPieceIdByComponent(const UPrimitiveComponent* Component) const;

	/**
	 * ロジックのグリッド座標を、このアクタが実際にボクセルを置くワールド座標へ直す。
	 * 中心合わせのオフセットとアクタの変換まで通すので、UpdatePlacements が置くインスタンスと同じ位置になる。
	 * ACubelithPlayerController がドラッグの感度（1 ボクセルの画面上の大きさ）を測るのに使う（RULES.md 3.3「移動」）
	 */
	FVector GridToWorldLocation(const Cubelith::FVec3& Voxel) const;

	/**
	 * 選択中のピースを置き換える（未選択は INDEX_NONE）。前に選んでいたピースは元の色へ戻る。
	 * 仮の見せ方として、選んだピースの色を白へ寄せて明るくする（縁取りや発光での本実装は U5）
	 */
	void SetSelectedPiece(int32 PieceId);

	/** 選択中のピース id（未選択は INDEX_NONE） */
	int32 GetSelectedPiece() const { return SelectedPieceId; }

	/**
	 * 表示だけの自由回転を掛ける（90 度に縛らない見せ方。pieces.ts の setFreeRotation）。
	 * ロジックの配置は変えないので、確定させるのは呼び出し側（ACubelithPlayerController が
	 * Cubelith::SnappedOrientation と Cubelith::FGame::Place で行い、そのあと ClearFreeRotation で解く）。
	 *
	 * WorldQuat は UE ワールド（Z が上・左手系。Docs/SPEC_UE.md 7.2 の変換を通した空間）の回転で、
	 * 回転の中心はそのピースの**局所原点**（RULES.md 3.3。Cubelith::FPlacement::Position がそのグリッド座標）。
	 * 90 度で確定したときに FGame::Rotate / FGame::Place と同じ中心で回るので、表示と論理がずれない。
	 *
	 * まだ一度も配置を受けていないピース（UpdatePlacements 前）に掛けても、次の UpdatePlacements で効く。
	 */
	void SetFreeRotation(int32 PieceId, const FQuat& WorldQuat);

	/** 自由回転を解いて、ロジックの配置どおりの見た目に戻す（掛かっていなければ何もしない） */
	void ClearFreeRotation(int32 PieceId);

	/**
	 * スナップ候補があるピースを置き換える（無ければ INDEX_NONE。pieces.ts の setSnapHint）。
	 * 候補がある間そのピースを薄く光らせる（RULES.md 5.1）。前に光らせていたピースは元の色へ戻る。
	 *
	 * 仮の見せ方として、選択の強調（SetSelectedPiece）と同じく動的マテリアルの色を持ち上げる
	 * （強さは SnapHintBrightnessScale。マテリアルの Emissive での本実装は U5）。
	 * **選択の強調と重なったときは選択が優先**（発光は「そこへ吸い付く」の予告で、選択より弱い情報。
	 * 選択中のピースは白へ寄って既に目立っているので、そこへ弱い持ち上げを重ねても見分けが付かない）。
	 */
	void SetSnapHint(int32 PieceId);

	/** スナップ候補があるピース id（無ければ INDEX_NONE） */
	int32 GetSnapHint() const { return SnapHintPieceId; }

	/**
	 * 表示だけをずらす（スナップの補間移動。pieces.ts の setOffset）。GridOffset はグリッド単位で、
	 * 1.0 = ボクセル 1 マス。ロジックの配置は動かさないので、補間中もクリア判定は整数座標のまま。
	 * まだ一度も配置を受けていないピースに掛けても、次の UpdatePlacements で効く。
	 */
	void SetViewOffset(int32 PieceId, const FVector& GridOffset);

	/** 表示だけのずれを解いて論理位置どおりに戻す（掛かっていなければ何もしない） */
	void ClearViewOffset(int32 PieceId);

	/**
	 * 固定（ロック）の鍵アイコンを出す / 消す（RULES.md 6 章。pieces.ts の setLockIcon）。
	 * Kind が未設定なら消す（固定していない状態もそのまま渡せる形にしてあるので、
	 * 呼び出し側は Cubelith::FGame::LockKindOf の戻り値をそのまま流せる）。
	 *
	 * アイコンは**そのピースの各ボクセルの中心**に 1 個ずつ出し、ピースが動けば追従する
	 * （UpdatePlacements と同じ経路で位置を書き直す）。色は銀 = 手動の固定 / 金 = ヒントで、
	 * メッシュ・大きさ・色はすべて UPROPERTY で人が差し替えられる（下の LockIcon* / *LockIconColor）。
	 *
	 * RULES.md 6 章の「そのピースの内部発光コアは消す」はここでは扱わない
	 * （内部発光コア自体が U5 で人が作るマテリアルなので、消す相手がまだ無い。Docs/SPEC_UE.md 4 章）。
	 */
	void SetLockIcon(int32 PieceId, const TOptional<Cubelith::ELockKind>& Kind);

	/** そのピースに出している鍵アイコンの種類（出していなければ未設定） */
	TOptional<Cubelith::ELockKind> GetLockIcon(int32 PieceId) const;

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

	/**
	 * 選択中のピースの色を白へ寄せる量（0 で元の色のまま、1 で真っ白）。
	 * 仮の強調なので、人がエディタで見え方を調整できるようにしてある（Docs/SPEC_UE.md 0 章）
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Render", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SelectionWhitenAmount = 0.5f;

	/** 選択中のピースの明るさの倍率（1 で元のまま）。白へ寄せたうえでさらに持ち上げる */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Render", meta = (ClampMin = "0.0"))
	float SelectionBrightnessScale = 1.6f;

	/**
	 * スナップ候補があるピースの明るさの倍率（1 で元のまま。RULES.md 5.1 の「薄く光る」）。
	 * 選択の強調（SelectionBrightnessScale）より弱くしておくと、選択と候補が見分けられる。
	 * 仮の強調なので、人がエディタで見え方を調整できるようにしてある（Docs/SPEC_UE.md 0 章）
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Render", meta = (ClampMin = "0.0"))
	float SnapHintBrightnessScale = 1.25f;

	/**
	 * 固定の鍵アイコンに使うメッシュ（RULES.md 6 章）。既定はエンジンの球 /Engine/BasicShapes/Sphere で、
	 * **本物の南京錠のメッシュ / アイコンは人が後で入れる**（`.uasset` は AI が作らない。Docs/SPEC_UE.md 0 章）。
	 * 空ならアイコンを出さない（警告は SetLockIcon のときに 1 回だけ出す）
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Lock")
	TObjectPtr<UStaticMesh> LockIconMesh;

	/**
	 * 鍵アイコンのマテリアル。既定はボクセルと同じ /Engine/BasicShapes/BasicShapeMaterial で、
	 * 固定の種類ごとに UMaterialInstanceDynamic を作って LockIconColorParameterName へ銀 / 金を流す
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Lock")
	TObjectPtr<UMaterialInterface> LockIconMaterial;

	/** 鍵アイコンの色を流し込むベクトルパラメータの名前（マテリアルに無いと色が付かない） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Lock")
	FName LockIconColorParameterName = TEXT("Color");

	/**
	 * 鍵アイコン 1 個の大きさの、ボクセル 1 マスに対する比（既定 0.5 = 半マス。
	 * lockIcons.ts の ICON_SIZE と同じ）。
	 *
	 * 解釈: ピースのボクセルは VoxelFillRatio（既定 0.96）でわずかに縮めてあるので、それより小さくすると
	 * アイコンがボクセルの中に見える。大きすぎると斜めから見たときに隣のアイコンと重なって
	 * ピースの形が読みにくくなる。最後は人がこの値で調整する
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Lock", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float LockIconSizeRatio = 0.5f;

	/**
	 * 手動の固定の鍵アイコンの色（RULES.md 6 章の「銀」。lockIcons.ts の manual の胴の色に寄せてある）。
	 * ヒントの金と一目で見分けられればよい仮の色なので、人がエディタで調整する
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Lock")
	FLinearColor ManualLockIconColor = FLinearColor(0.85f, 0.88f, 0.94f);

	/** ヒントの固定の鍵アイコンの色（RULES.md 6 章の「金」。lockIcons.ts の hint の胴の色に寄せてある） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Lock")
	FLinearColor HintLockIconColor = FLinearColor(1.0f, 0.78f, 0.12f);

private:
	/**
	 * 1 ピース分のインスタンスを書き直す（pieces.ts の applyPlacement）。表示だけの自由回転はここで足す。
	 * 戻り値はアクタの原点から最も遠いボクセルまでの距離の 2 乗（外接球の半径を測るのに使う。自由回転は数えない）。
	 * 未知のピース id なら警告を出して 0 を返す。
	 */
	double ApplyPlacement(const Cubelith::FPlacement& Placement, const FVector& CenterOffset, const FVector& InstanceScale);

	/**
	 * 直近の配置でそのピースだけ書き直す（自由回転・表示だけのずれを掛け外ししたとき）。
	 * まだ置いていなければ何もしない
	 */
	void RedrawPiece(int32 PieceId);

	/** インスタンス 1 個のスケール（メッシュの 1 辺をボクセル 1 マスにしてから VoxelFillRatio だけ縮める） */
	FVector GetInstanceScale() const;

	/** ピース index からピース色を作る（pieces.ts の pieceColor） */
	FLinearColor MakePieceColor(int32 Index, int32 Count) const;

	/** メッシュ 1 個の 1 辺の長さ（cm）。インスタンスのスケールをここから決める */
	double GetVoxelMeshSizeCm() const;

	/**
	 * 鍵アイコンのインスタンスを種類ごとに作り直す（固定中のピースは多くても 27 個・アイコンは
	 * 合わせても N^3 個なので、差分を追うより毎回作り直すほうが単純で取り違えが起きない）。
	 * 呼ぶのは「アイコンの有無 / 種類が変わったとき」と「アイコンを出しているピースの配置が変わったとき」だけ
	 */
	void RebuildLockIcons();

	/** 固定の種類に対応する鍵アイコンのコンポーネント（Build 前なら nullptr） */
	UInstancedStaticMeshComponent* GetLockIconMeshComponent(Cubelith::ELockKind Kind) const;

	/** ピース 1 個分の鍵アイコンの変換を、そのピースの各ボクセルの中心に 1 個ずつ足す */
	void AppendLockIconTransforms(
		int32 PieceId, const FVector& CenterOffset, const FVector& IconScale, TArray<FTransform>& OutTransforms) const;

	/** 鍵アイコン 1 個のスケール（メッシュの 1 辺をボクセル 1 マスにしてから LockIconSizeRatio だけ縮める） */
	FVector GetLockIconScale() const;

	/** 鍵アイコンのメッシュ 1 個の 1 辺の長さ（cm）。GetVoxelMeshSizeCm と同じ測り方 */
	double GetLockIconMeshSizeCm() const;

	/** 鍵アイコンのコンポーネント 1 つを作る（Build から種類ごとに呼ぶ）。作れなければ nullptr */
	UInstancedStaticMeshComponent* CreateLockIconMeshComponent(Cubelith::ELockKind Kind);

	/** ピース id からピースを引く。未知の id なら nullptr */
	const Cubelith::FPiece* FindPiece(int32 PieceId) const;

	/**
	 * ピース id の動的マテリアルへ色を流す。今の選択（SelectedPieceId）とスナップ候補（SnapHintPieceId）を
	 * 見て、強調の要る / 要らないをここで決める（呼び出し側は「このピースを塗り直して」だけを言う）
	 */
	void ApplyPieceColor(int32 PieceId);

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

	/** ピース id → そのピースの動的マテリアル。選択の強調で色を差し替える（VoxelMaterial が空なら入らない） */
	UPROPERTY()
	TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> PieceMaterials;

	/** ピース id → 選択していないときの色。強調を解くときにここへ戻す */
	TMap<int32, FLinearColor> PieceBaseColors;

	/**
	 * ピース id → 直近の UpdatePlacements で受けた配置（pieces.ts の lastPlacements）。
	 * 自由回転を掛け外しするときに、そのピースだけを同じ配置で書き直すのに使う
	 */
	TMap<int32, Cubelith::FPlacement> LastPlacements;

	/** ピース id → 表示だけの自由回転（掛かっているピースだけ入る。pieces.ts の freeRotations） */
	TMap<int32, FQuat> FreeRotations;

	/**
	 * ピース id → 表示だけのずれ（グリッド単位。掛かっているピースだけ入る。pieces.ts の offsets）。
	 * スナップの補間移動（Cubelith::FSnapMotion）が毎フレーム書き換える
	 */
	TMap<int32, FVector> ViewOffsets;

	/**
	 * 鍵アイコンを出しているピース id → 固定の種類（pieces.ts の lockIcons）。
	 * 出していないピースは入らない（= 固定していないピース）
	 */
	TMap<int32, Cubelith::ELockKind> LockIcons;

	/**
	 * 手動の固定の鍵アイコン（銀）のインスタンス群。**固定の種類ごとに 1 コンポーネント**にしてあるので、
	 * 増えるドローコールは最大 2 つで済む（ボクセルごとにコンポーネントを作らない。lockIcons.ts と同じ考え）
	 */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ManualLockIcons;

	/** ヒントの固定の鍵アイコン（金）のインスタンス群 */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> HintLockIcons;

	/** スナップ候補があるピース id（無ければ INDEX_NONE。pieces.ts の snapHinted） */
	int32 SnapHintPieceId = INDEX_NONE;

	/** 選択中のピース id（未選択は INDEX_NONE）。Build で作り直したら未選択に戻る */
	int32 SelectedPieceId = INDEX_NONE;

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
