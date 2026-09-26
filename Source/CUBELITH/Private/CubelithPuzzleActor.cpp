#include "CubelithPuzzleActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "UObject/ConstructorHelpers.h"

#include "CubelithCoords.h"
#include "CubelithLog.h"

namespace
{
	/**
	 * 既定のメッシュ（エンジンの立方体 /Engine/BasicShapes/Cube）の 1 辺（cm）。
	 * 前提: エンジンの立方体は 1 辺 100 cm で、ボクセル 1 マス（Cubelith::VoxelSizeCm）と同じ大きさ。
	 * メッシュを差し替えても破綻しないよう、実際のスケールは GetVoxelMeshSizeCm が境界の大きさから求め、
	 * この値は境界が取れなかったときの逃げ道としてだけ使う。
	 */
	constexpr double DefaultVoxelMeshSizeCm = 100.0;

	/**
	 * 解答空間 [0, N-1]^3 の中心をアクタの原点へ持ってくるオフセット（cm）。
	 *
	 * ボクセル (0,0,0)..(N-1,N-1,N-1) の中心は (N-1)/2 マスの位置にあるので、その分だけ引くと
	 * 立方体の中心がアクタの原点 = 軌道カメラの注視点に来る（pieces.ts の
	 * object.position.setScalar(-(n - 1) / 2) と同じ。あちらは 1 マス = 1 単位なので cm を掛けない）。
	 * 3 軸とも同じ量なので、7.2 の y と z の入れ替え（VoxelToWorld）を通しても値は変わらない。
	 */
	FVector SolutionSpaceCenterOffset(int32 N)
	{
		const double Offset = -static_cast<double>(N - 1) * 0.5 * Cubelith::VoxelSizeCm;
		return FVector(Offset, Offset, Offset);
	}
}

ACubelithPuzzleActor::ACubelithPuzzleActor()
{
	// 配置が変わったときだけ書き直す（毎フレームの処理は無い）
	PrimaryActorTick.bCanEverTick = false;

	PuzzleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PuzzleRoot"));
	// 実行時に湧かせてインスタンスを書き換えるので Movable。子のコンポーネントも Movable にする
	PuzzleRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(PuzzleRoot);

	// 既定のメッシュ・マテリアルはエンジンの基本アセットをパス文字列で引くだけ（.uasset は作らない。
	// Docs/SPEC_UE.md 0 章）。どちらも UPROPERTY(EditAnywhere) なので人がエディタで差し替えられる
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultVoxelMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (DefaultVoxelMesh.Succeeded())
	{
		VoxelMesh = DefaultVoxelMesh.Object;
	}
	else
	{
		UE_LOG(LogCubelith, Warning,
			TEXT("既定のボクセルメッシュ /Engine/BasicShapes/Cube.Cube が見つからない。VoxelMesh を差し替えること"));
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultVoxelMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (DefaultVoxelMaterial.Succeeded())
	{
		VoxelMaterial = DefaultVoxelMaterial.Object;
	}
	else
	{
		UE_LOG(LogCubelith, Warning,
			TEXT("既定のマテリアル /Engine/BasicShapes/BasicShapeMaterial が見つからない。VoxelMaterial を差し替えること"));
	}
}

void ACubelithPuzzleActor::Build(TArrayView<const Cubelith::FPiece> Pieces, int32 N)
{
	// 作り直しに備えて前の分を片付ける（U2 では 1 回だけ呼ぶが、U4 の「次の問題」で作り直す道が要る）
	for (const TPair<int32, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : PieceMeshes)
	{
		if (UInstancedStaticMeshComponent* Existing = Pair.Value.Get())
		{
			Existing->DestroyComponent();
		}
	}
	if (PieceMeshes.Num() > 0)
	{
		++BuildGeneration;
	}
	PieceMeshes.Reset();
	PieceIndexById.Reset();
	PieceList.Reset();
	BoundingRadiusCm = 0.0;

	SpaceSize = N;

	if (VoxelMesh == nullptr)
	{
		UE_LOG(LogCubelith, Warning, TEXT("VoxelMesh が空なのでピースが描かれない。エディタで設定すること"));
	}

	// マテリアルにピース色のパラメータが無いと、全ピースが同じ色になって形の境目が分からない。
	// 人がエディタで差し替えられるよう、Build ごとに 1 回だけ警告を出す（ピースごとには出さない）
	if (VoxelMaterial != nullptr)
	{
		FLinearColor Unused = FLinearColor::White;
		if (!VoxelMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(ColorParameterName), Unused))
		{
			UE_LOG(LogCubelith, Warning,
				TEXT("マテリアルに %s パラメータが無いのでピースの色が付かない。VoxelMaterial / ColorParameterName を差し替えること"),
				*ColorParameterName.ToString());
		}
	}
	else
	{
		UE_LOG(LogCubelith, Warning, TEXT("VoxelMaterial が空なのでピースの色が付かない。エディタで設定すること"));
	}

	PieceList.Reserve(Pieces.Num());
	PieceIndexById.Reserve(Pieces.Num());
	PieceMeshes.Reserve(Pieces.Num());

	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		const Cubelith::FPiece& Piece = Pieces[Index];
		if (PieceIndexById.Contains(Piece.Id))
		{
			UE_LOG(LogCubelith, Warning, TEXT("ピース id %d が重複しているので後の方を捨てた"), Piece.Id);
			continue;
		}

		const int32 StoredIndex = PieceList.Add(Piece);
		PieceIndexById.Add(Piece.Id, StoredIndex);

		// 名前に id を入れて、エディタのアウトライナや U3 のライントレースから元のピースが分かるようにする
		const FString ComponentName = (BuildGeneration == 0)
			? FString::Printf(TEXT("Piece_%d"), Piece.Id)
			: FString::Printf(TEXT("Piece_%d_g%d"), Piece.Id, BuildGeneration);

		UInstancedStaticMeshComponent* Mesh = NewObject<UInstancedStaticMeshComponent>(
			this, UInstancedStaticMeshComponent::StaticClass(), FName(*ComponentName));
		Mesh->SetupAttachment(PuzzleRoot);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(VoxelMesh);
		// コリジョンの設定は既定のまま触らない（U3 のライントレースでピースを選ぶのに使う）

		if (VoxelMaterial != nullptr)
		{
			// ピースごとに色を変えるため、ピースごとに動的マテリアルインスタンスを 1 つ作る
			// （元のマテリアルをそのまま割り当てると全ピースが同じ色になる）
			UMaterialInstanceDynamic* PieceMaterial = UMaterialInstanceDynamic::Create(VoxelMaterial, this);
			if (PieceMaterial != nullptr)
			{
				PieceMaterial->SetVectorParameterValue(ColorParameterName, MakePieceColor(Index, Pieces.Num()));
				Mesh->SetMaterial(0, PieceMaterial);
			}
		}

		Mesh->RegisterComponent();
		PieceMeshes.Add(Piece.Id, Mesh);
	}

	UE_LOG(LogCubelith, Verbose, TEXT("ピースのコンポーネントを %d 個作った（N=%d）"), PieceMeshes.Num(), SpaceSize);
}

void ACubelithPuzzleActor::UpdatePlacements(TArrayView<const Cubelith::FPlacement> Placements)
{
	if (PieceMeshes.Num() == 0)
	{
		UE_LOG(LogCubelith, Warning, TEXT("Build を呼ぶ前に UpdatePlacements が呼ばれた"));
		return;
	}

	const FVector CenterOffset = SolutionSpaceCenterOffset(SpaceSize);
	// メッシュの 1 辺をボクセル 1 マス分にしてから、境目が見えるよう VoxelFillRatio だけ縮める
	const double Scale = static_cast<double>(VoxelFillRatio) * Cubelith::VoxelSizeCm / GetVoxelMeshSizeCm();
	const FVector InstanceScale(Scale, Scale, Scale);

	double MaxDistanceSquared = 0.0;

	for (const Cubelith::FPlacement& Placement : Placements)
	{
		const Cubelith::FPiece* Piece = FindPiece(Placement.PieceId);
		TObjectPtr<UInstancedStaticMeshComponent>* Found = PieceMeshes.Find(Placement.PieceId);
		UInstancedStaticMeshComponent* Mesh = (Found != nullptr) ? Found->Get() : nullptr;
		if (Piece == nullptr || Mesh == nullptr)
		{
			UE_LOG(LogCubelith, Warning, TEXT("未知のピース id %d の配置が来た"), Placement.PieceId);
			continue;
		}

		// 向きは PlacedVoxels がワールドのボクセル座標に織り込むので、インスタンスは平行移動だけでよい
		// （pieces.ts の通常時と同じ）。Cubelith::OrientationToWorldQuat は U3 の自由回転（指で回している
		// 間の、90 度に縛られない見た目）で使うためのもので、ここでは要らない
		const TArray<Cubelith::FVec3> Voxels = Cubelith::PlacedVoxels(*Piece, Placement);

		for (int32 Index = 0; Index < Voxels.Num(); ++Index)
		{
			// アクタはワールド原点に置くので、このローカル座標がそのままワールド座標になる
			const FVector Location = Cubelith::VoxelToWorld(Voxels[Index]) + CenterOffset;
			MaxDistanceSquared = FMath::Max(MaxDistanceSquared, Location.SizeSquared());

			const FTransform InstanceTransform(FQuat::Identity, Location, InstanceScale);
			if (Index < Mesh->GetInstanceCount())
			{
				// 描画への反映（MarkRenderStateDirty）は 1 ピース分を書き終えてから 1 回だけ行う
				Mesh->UpdateInstanceTransform(Index, InstanceTransform, /*bWorldSpace=*/false,
					/*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);
			}
			else
			{
				Mesh->AddInstance(InstanceTransform, /*bWorldSpace=*/false);
			}
		}

		// ピースのボクセル数は変わらないので普通は起きないが、余っていれば後ろから捨てる
		for (int32 Index = Mesh->GetInstanceCount() - 1; Index >= Voxels.Num(); --Index)
		{
			Mesh->RemoveInstance(Index);
		}

		Mesh->MarkRenderStateDirty();
	}

	if (MaxDistanceSquared > 0.0)
	{
		// 求めたのはボクセルの中心までの距離なので、端まで入るようボクセル 1 個分を足す
		BoundingRadiusCm = FMath::Sqrt(MaxDistanceSquared) + Cubelith::VoxelSizeCm;
	}
}

FLinearColor ACubelithPuzzleActor::MakePieceColor(int32 Index, int32 Count) const
{
	// pieces.ts の pieceColor と同じ組み立て: 色相を等間隔にずらして見分けを付ける
	const float Hue = FMath::Frac(
		static_cast<float>(Index) / static_cast<float>(FMath::Max(Count, 1)) + ColorHueOffset);

	// 解釈: pieces.ts は HSL（S=0.62・L=0.58）だが、UE の HSVToLinearRGB は HSV なので同じ数値でも
	// 同じ色にはならない（HSV の S・V の方が濃く沈んだ色になる）。色は見分けが付けば足りる仮のもので
	// 最後は人が調整する値なので、pieces.ts の数値をそのまま既定値に入れて EditAnywhere で公開する。
	// HSVToLinearRGB は R を色相（度）・G を彩度・B を明度として読む
	return FLinearColor(Hue * 360.0f, ColorSaturation, ColorValue).HSVToLinearRGB();
}

double ACubelithPuzzleActor::GetVoxelMeshSizeCm() const
{
	if (VoxelMesh != nullptr)
	{
		// 境界の最も長い辺をメッシュの 1 辺とみなす（立方体なら 3 辺とも同じ）。
		// エンジンの立方体では 100 cm になり、別のメッシュに差し替えても大きさが合う
		const FBoxSphereBounds Bounds = VoxelMesh->GetBounds();
		const double LongestSide =
			2.0 * FMath::Max3(Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z);
		if (LongestSide > UE_DOUBLE_SMALL_NUMBER)
		{
			return LongestSide;
		}
	}

	return DefaultVoxelMeshSizeCm;
}

const Cubelith::FPiece* ACubelithPuzzleActor::FindPiece(int32 PieceId) const
{
	const int32* Index = PieceIndexById.Find(PieceId);
	return (Index != nullptr) ? &PieceList[*Index] : nullptr;
}
