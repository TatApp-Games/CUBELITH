#include "CubelithPuzzleActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "UObject/ConstructorHelpers.h"

#include "CubelithCoords.h"
#include "CubelithLockOps.h"
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

	// 固定の鍵アイコンの仮の形（RULES.md 6 章）。**本物の南京錠のメッシュ / アイコンは人が後で入れる**ので、
	// 既定はエンジンの球にしておく（立方体だとボクセルと形が同じで「別のもの」に見えない）。
	// マテリアルはボクセルと共用し、種類ごとの動的マテリアルに銀 / 金を流す（Build で作る）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultLockIconMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (DefaultLockIconMesh.Succeeded())
	{
		LockIconMesh = DefaultLockIconMesh.Object;
	}
	else
	{
		UE_LOG(LogCubelith, Warning,
			TEXT("既定の鍵アイコンのメッシュ /Engine/BasicShapes/Sphere.Sphere が見つからない。LockIconMesh を差し替えること"));
	}

	if (DefaultVoxelMaterial.Succeeded())
	{
		LockIconMaterial = DefaultVoxelMaterial.Object;
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
	// 鍵アイコンのコンポーネントも作り直す（ピースと同じ理由。前の盤面の固定は残さない）
	if (ManualLockIcons != nullptr)
	{
		ManualLockIcons->DestroyComponent();
		ManualLockIcons = nullptr;
	}
	if (HintLockIcons != nullptr)
	{
		HintLockIcons->DestroyComponent();
		HintLockIcons = nullptr;
	}
	if (PieceMeshes.Num() > 0)
	{
		++BuildGeneration;
	}
	PieceMeshes.Reset();
	PieceIndexById.Reset();
	PieceList.Reset();
	PieceMaterials.Reset();
	PieceBaseColors.Reset();
	LastPlacements.Reset();
	// 作り直したら表示だけの自由回転も消える（掛けていた操作は外で畳まれている）
	FreeRotations.Reset();
	// スナップの補間も同じ（走っていた補間は外で打ち切られている。Cubelith::FSnapMotion::CancelAll）
	ViewOffsets.Reset();
	// 作り直したら固定も無くなる（新しい盤面の固定は呼び出し側が SetLockIcon で入れ直す）
	LockIcons.Reset();
	// 作り直したら選択は無くなる（選び直すのは操作する側。RULES.md 3.3 の外からの解除に当たる）
	SelectedPieceId = INDEX_NONE;
	SnapHintPieceId = INDEX_NONE;
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
	PieceMaterials.Reserve(Pieces.Num());
	PieceBaseColors.Reserve(Pieces.Num());

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

		// ピースを押して選べるよう、ライントレース（ECC_Visibility）に当たるようにする。
		// 物理は使わない（RULES.md 3.4）ので当たり判定はクエリ専用に留め、重なりの通知も要らない。
		// 撃つのは簡易コリジョン（立方体の箱）なので bTraceComplex は使わない側のまま
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionObjectType(ECC_WorldDynamic);
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);

		if (VoxelMaterial != nullptr)
		{
			// ピースごとに色を変えるため、ピースごとに動的マテリアルインスタンスを 1 つ作る
			// （元のマテリアルをそのまま割り当てると全ピースが同じ色になる）
			UMaterialInstanceDynamic* PieceMaterial = UMaterialInstanceDynamic::Create(VoxelMaterial, this);
			if (PieceMaterial != nullptr)
			{
				// 選択の強調を解くときに戻せるよう、素の色とマテリアルを覚えておく
				const FLinearColor BaseColor = MakePieceColor(Index, Pieces.Num());
				PieceMaterial->SetVectorParameterValue(ColorParameterName, BaseColor);
				Mesh->SetMaterial(0, PieceMaterial);
				PieceMaterials.Add(Piece.Id, PieceMaterial);
				PieceBaseColors.Add(Piece.Id, BaseColor);
			}
		}

		Mesh->RegisterComponent();
		PieceMeshes.Add(Piece.Id, Mesh);
	}

	// 鍵アイコンは固定の種類ごとに 1 コンポーネント（ドローコールは最大 2 つで済む。RULES.md 6 章）。
	// 作るだけでインスタンスは置かない（固定が付いた時点で SetLockIcon から RebuildLockIcons が置く）
	ManualLockIcons = CreateLockIconMeshComponent(Cubelith::ELockKind::Manual);
	HintLockIcons = CreateLockIconMeshComponent(Cubelith::ELockKind::Hint);

	UE_LOG(LogCubelith, Verbose, TEXT("ピースのコンポーネントを %d 個作った（N=%d）"), PieceMeshes.Num(), SpaceSize);
}

UInstancedStaticMeshComponent* ACubelithPuzzleActor::CreateLockIconMeshComponent(Cubelith::ELockKind Kind)
{
	const bool bHint = (Kind == Cubelith::ELockKind::Hint);
	const TCHAR* const KindName = bHint ? TEXT("Hint") : TEXT("Manual");

	// 名前に世代を付けるのはピースと同じ理由（前の Build のコンポーネントが GC 待ちで名前を握っていることがある）
	const FString ComponentName = (BuildGeneration == 0)
		? FString::Printf(TEXT("LockIcon_%s"), KindName)
		: FString::Printf(TEXT("LockIcon_%s_g%d"), KindName, BuildGeneration);

	UInstancedStaticMeshComponent* Mesh = NewObject<UInstancedStaticMeshComponent>(
		this, UInstancedStaticMeshComponent::StaticClass(), FName(*ComponentName));
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	Mesh->SetupAttachment(PuzzleRoot);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(LockIconMesh);

	// アイコンは見せるだけ。ピックはピース本体で拾うので、ライントレースに当たらせない
	// （当たると固定中のピースを押したときにアイコンがピースの手前で遮ってしまう）
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);

	if (LockIconMaterial != nullptr)
	{
		UMaterialInstanceDynamic* IconMaterial = UMaterialInstanceDynamic::Create(LockIconMaterial, this);
		if (IconMaterial != nullptr)
		{
			// 銀 / 金のどちらを使うかは Cubelith::LockIconColor が決める（色そのものは人が調整する UPROPERTY）
			IconMaterial->SetVectorParameterValue(LockIconColorParameterName,
				Cubelith::LockIconColor(Kind, ManualLockIconColor, HintLockIconColor));
			Mesh->SetMaterial(0, IconMaterial);
		}
	}

	Mesh->RegisterComponent();
	return Mesh;
}

void ACubelithPuzzleActor::UpdatePlacements(TArrayView<const Cubelith::FPlacement> Placements)
{
	if (PieceMeshes.Num() == 0)
	{
		UE_LOG(LogCubelith, Warning, TEXT("Build を呼ぶ前に UpdatePlacements が呼ばれた"));
		return;
	}

	const FVector CenterOffset = SolutionSpaceCenterOffset(SpaceSize);
	const FVector InstanceScale = GetInstanceScale();

	double MaxDistanceSquared = 0.0;

	for (const Cubelith::FPlacement& Placement : Placements)
	{
		MaxDistanceSquared =
			FMath::Max(MaxDistanceSquared, ApplyPlacement(Placement, CenterOffset, InstanceScale));
	}

	if (MaxDistanceSquared > 0.0)
	{
		// 求めたのはボクセルの中心までの距離なので、端まで入るようボクセル 1 個分を足す
		BoundingRadiusCm = FMath::Sqrt(MaxDistanceSquared) + Cubelith::VoxelSizeCm;
	}

	// 鍵アイコンはボクセルの中心に付いているので、配置が動いたら一緒に書き直す（ピースに追従する）。
	// 固定が 1 つも無ければ何もしない ＝ 普段のドラッグでこの経路の負荷は増えない
	if (LockIcons.Num() > 0)
	{
		RebuildLockIcons();
	}
}

double ACubelithPuzzleActor::ApplyPlacement(
	const Cubelith::FPlacement& Placement, const FVector& CenterOffset, const FVector& InstanceScale)
{
	const Cubelith::FPiece* Piece = FindPiece(Placement.PieceId);
	TObjectPtr<UInstancedStaticMeshComponent>* Found = PieceMeshes.Find(Placement.PieceId);
	UInstancedStaticMeshComponent* Mesh = (Found != nullptr) ? Found->Get() : nullptr;
	if (Piece == nullptr || Mesh == nullptr)
	{
		UE_LOG(LogCubelith, Warning, TEXT("未知のピース id %d の配置が来た"), Placement.PieceId);
		return 0.0;
	}

	// 自由回転を掛け外ししたときに同じ配置で書き直せるよう控えておく（pieces.ts の lastPlacements）
	LastPlacements.Add(Placement.PieceId, Placement);

	// 表示だけの自由回転（掛かっていなければ nullptr）。90 度に縛らない見せ方で、ロジックの配置は変わらない
	const FQuat* FreeRotation = FreeRotations.Find(Placement.PieceId);

	// 表示だけのずれ（スナップの補間移動。掛かっていなければ 0）。グリッド単位で来るので UE の座標へ直す。
	// Cubelith::VoxelToWorld は整数座標（FVec3）用なので、同じ置換（(x, y, z) → (X, Z, Y)）を
	// 小数のままここで掛ける（Docs/SPEC_UE.md 7.2）
	const FVector* GridOffset = ViewOffsets.Find(Placement.PieceId);
	const FVector ViewOffsetCm = (GridOffset != nullptr)
		? FVector(GridOffset->X, GridOffset->Z, GridOffset->Y) * Cubelith::VoxelSizeCm
		: FVector::ZeroVector;

	// 向きは PlacedVoxels がワールドのボクセル座標に織り込むので、通常時のインスタンスは平行移動だけでよい
	// （pieces.ts の通常時と同じ）
	const TArray<Cubelith::FVec3> Voxels = Cubelith::PlacedVoxels(*Piece, Placement);

	// 自由回転の中心はピースの局所原点（RULES.md 3.3。Placement.Position がそのグリッド座標）。
	// FGame::Rotate / FGame::Place は Position を据え置いて向き id だけを差し替える = 局所原点まわりの回転なので、
	// 見せ方も同じ中心で回さないと確定した瞬間にピースが飛ぶ
	// 表示だけのずれが掛かっていれば中心も同じだけずれる（両方同時に掛かることは無いが、辻褄は合わせておく）
	const FVector FreeRotationCenter = Cubelith::VoxelToWorld(Placement.Position) + CenterOffset + ViewOffsetCm;

	double MaxDistanceSquared = 0.0;

	for (int32 Index = 0; Index < Voxels.Num(); ++Index)
	{
		// アクタはワールド原点に置くので、このローカル座標がそのままワールド座標になる
		const FVector LogicLocation = Cubelith::VoxelToWorld(Voxels[Index]) + CenterOffset;
		// 外接球は「ロジックの配置での大きさ」を測るものなので、一時的な自由回転とずれは数えない
		MaxDistanceSquared = FMath::Max(MaxDistanceSquared, LogicLocation.SizeSquared());

		FVector Location = LogicLocation + ViewOffsetCm;

		FQuat InstanceRotation = FQuat::Identity;
		if (FreeRotation != nullptr)
		{
			// 局所原点まわりに回した位置へ、ボクセル自身も同じだけ回して置く（pieces.ts の回転モード中と同じ）
			Location = FreeRotationCenter + FreeRotation->RotateVector(Location - FreeRotationCenter);
			InstanceRotation = *FreeRotation;
		}

		const FTransform InstanceTransform(InstanceRotation, Location, InstanceScale);
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

	return MaxDistanceSquared;
}

void ACubelithPuzzleActor::SetFreeRotation(int32 PieceId, const FQuat& WorldQuat)
{
	// 回転を掛けるのは行列の作り直しなので、同じ向きで呼ばれたら書き直さない（ドラッグ中は毎フレーム通る）
	if (const FQuat* Existing = FreeRotations.Find(PieceId))
	{
		if (Existing->Equals(WorldQuat))
		{
			return;
		}
	}

	FreeRotations.Add(PieceId, WorldQuat.GetNormalized());
	RedrawPiece(PieceId);
}

void ACubelithPuzzleActor::ClearFreeRotation(int32 PieceId)
{
	// 掛かっていなければ書き直す必要も無い（通常時の負荷を増やさない。pieces.ts の setFreeRotation(null) と同じ）
	if (FreeRotations.Remove(PieceId) == 0)
	{
		return;
	}

	RedrawPiece(PieceId);
}

void ACubelithPuzzleActor::SetSnapHint(int32 PieceId)
{
	if (SnapHintPieceId == PieceId)
	{
		return;
	}

	const int32 PreviousPieceId = SnapHintPieceId;
	SnapHintPieceId = PieceId;

	// 先に前の候補を元の色へ戻してから新しい候補を光らせる（SetSelectedPiece と同じ順）
	if (PreviousPieceId != INDEX_NONE)
	{
		ApplyPieceColor(PreviousPieceId);
	}
	if (SnapHintPieceId != INDEX_NONE)
	{
		ApplyPieceColor(SnapHintPieceId);
	}
}

void ACubelithPuzzleActor::SetViewOffset(int32 PieceId, const FVector& GridOffset)
{
	// 補間中は毎フレーム来るので、同じ値なら書き直さない（インスタンスの行列を作り直す処理を省く）
	if (const FVector* Existing = ViewOffsets.Find(PieceId))
	{
		if (Existing->Equals(GridOffset))
		{
			return;
		}
	}

	ViewOffsets.Add(PieceId, GridOffset);
	RedrawPiece(PieceId);
}

void ACubelithPuzzleActor::ClearViewOffset(int32 PieceId)
{
	// 掛かっていなければ書き直す必要も無い（pieces.ts の setOffset(pieceId, null) と同じ）
	if (ViewOffsets.Remove(PieceId) == 0)
	{
		return;
	}

	RedrawPiece(PieceId);
}

void ACubelithPuzzleActor::RedrawPiece(int32 PieceId)
{
	const Cubelith::FPlacement* Found = LastPlacements.Find(PieceId);
	if (Found == nullptr)
	{
		// まだ一度も配置を受けていない。次の UpdatePlacements で自由回転ごと反映される
		return;
	}

	// ApplyPlacement が LastPlacements を書き換えるので、参照ではなく写しを渡す
	const Cubelith::FPlacement Placement = *Found;
	ApplyPlacement(Placement, SolutionSpaceCenterOffset(SpaceSize), GetInstanceScale());

	// このピースにアイコンが付いていればそれも追従させる（固定中のピースは動かないので普段は通らない）
	if (LockIcons.Contains(PieceId))
	{
		RebuildLockIcons();
	}
}

void ACubelithPuzzleActor::SetLockIcon(int32 PieceId, const TOptional<Cubelith::ELockKind>& Kind)
{
	if (!PieceIndexById.Contains(PieceId))
	{
		// TS の setLockIcon は throw する。UE 側は落とさずに警告だけ出す（表示の話なので遊べる状態を壊さない）
		UE_LOG(LogCubelith, Warning, TEXT("未知のピース id %d に鍵アイコンを設定しようとした"), PieceId);
		return;
	}

	// 変わっていなければ作り直さない（固定の状態を毎回まとめて流し込む呼び出し方でも余計な書き直しが起きない）
	const Cubelith::ELockKind* Existing = LockIcons.Find(PieceId);
	if (!Kind.IsSet())
	{
		if (Existing == nullptr)
		{
			return;
		}
		LockIcons.Remove(PieceId);
	}
	else
	{
		if (Existing != nullptr && *Existing == Kind.GetValue())
		{
			return;
		}
		LockIcons.Add(PieceId, Kind.GetValue());
	}

	if (LockIconMesh == nullptr)
	{
		// 人がまだメッシュを差し替えていない（既定のエンジンの球も見つからなかった場合）。
		// 状態だけは持っておく（後で差し替えて Build し直せば出る）
		UE_LOG(LogCubelith, Warning, TEXT("LockIconMesh が空なので固定の鍵アイコンが出ない。エディタで設定すること"));
		return;
	}

	RebuildLockIcons();
}

TOptional<Cubelith::ELockKind> ACubelithPuzzleActor::GetLockIcon(int32 PieceId) const
{
	const Cubelith::ELockKind* Found = LockIcons.Find(PieceId);
	return (Found != nullptr) ? TOptional<Cubelith::ELockKind>(*Found) : TOptional<Cubelith::ELockKind>();
}

UInstancedStaticMeshComponent* ACubelithPuzzleActor::GetLockIconMeshComponent(Cubelith::ELockKind Kind) const
{
	return (Kind == Cubelith::ELockKind::Hint) ? HintLockIcons.Get() : ManualLockIcons.Get();
}

void ACubelithPuzzleActor::RebuildLockIcons()
{
	const FVector CenterOffset = SolutionSpaceCenterOffset(SpaceSize);
	const FVector IconScale = GetLockIconScale();

	const Cubelith::ELockKind Kinds[2] = { Cubelith::ELockKind::Manual, Cubelith::ELockKind::Hint };

	TArray<FTransform> Transforms;

	for (const Cubelith::ELockKind Kind : Kinds)
	{
		UInstancedStaticMeshComponent* Mesh = GetLockIconMeshComponent(Kind);
		if (Mesh == nullptr)
		{
			// Build を呼ぶ前（コンポーネントがまだ無い）。次の Build のあとに出る
			continue;
		}

		Transforms.Reset();
		for (const TPair<int32, Cubelith::ELockKind>& Pair : LockIcons)
		{
			if (Pair.Value == Kind)
			{
				AppendLockIconTransforms(Pair.Key, CenterOffset, IconScale, Transforms);
			}
		}

		// その種類の固定が 1 つも無ければインスタンスが 0 個になる ＝ 描かれない（lockIcons.ts と同じ振る舞い）
		Mesh->ClearInstances();
		for (const FTransform& Transform : Transforms)
		{
			Mesh->AddInstance(Transform, /*bWorldSpace=*/false);
		}
	}
}

void ACubelithPuzzleActor::AppendLockIconTransforms(
	int32 PieceId, const FVector& CenterOffset, const FVector& IconScale, TArray<FTransform>& OutTransforms) const
{
	const Cubelith::FPiece* Piece = FindPiece(PieceId);
	const Cubelith::FPlacement* Placement = LastPlacements.Find(PieceId);
	if (Piece == nullptr || Placement == nullptr)
	{
		// まだ一度も配置を受けていない。次の UpdatePlacements でアイコンごと出る
		return;
	}

	// 表示だけのずれ（スナップの補間）が掛かっていれば同じだけずらす（ApplyPlacement と同じ置換）。
	// 自由回転は見ない: 固定中のピースは回せない（RULES.md 3.3）ので掛かっていることが無く、
	// 固定の直前に走っていた分は呼び出し側が確定させてから固定する（ACubelithGameMode::ToggleLock）
	const FVector* GridOffset = ViewOffsets.Find(PieceId);
	const FVector ViewOffsetCm = (GridOffset != nullptr)
		? FVector(GridOffset->X, GridOffset->Z, GridOffset->Y) * Cubelith::VoxelSizeCm
		: FVector::ZeroVector;

	const TArray<Cubelith::FVec3> Voxels = Cubelith::PlacedVoxels(*Piece, *Placement);
	OutTransforms.Reserve(OutTransforms.Num() + Voxels.Num());

	for (const Cubelith::FVec3& Voxel : Voxels)
	{
		// ボクセルの中心（RULES.md 6 章）。ボクセル本体より小さいので中に見える（LockIconSizeRatio）
		const FVector Location = Cubelith::VoxelToWorld(Voxel) + CenterOffset + ViewOffsetCm;
		OutTransforms.Emplace(FQuat::Identity, Location, IconScale);
	}
}

FVector ACubelithPuzzleActor::GetLockIconScale() const
{
	const double Scale = static_cast<double>(LockIconSizeRatio) * Cubelith::VoxelSizeCm / GetLockIconMeshSizeCm();
	return FVector(Scale, Scale, Scale);
}

double ACubelithPuzzleActor::GetLockIconMeshSizeCm() const
{
	if (LockIconMesh != nullptr)
	{
		// GetVoxelMeshSizeCm と同じ測り方（境界の最も長い辺をメッシュの 1 辺とみなす）。
		// 人が別のメッシュ / 板ポリのアイコンに差し替えても大きさが合う
		const FBoxSphereBounds Bounds = LockIconMesh->GetBounds();
		const double LongestSide =
			2.0 * FMath::Max3(Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z);
		if (LongestSide > UE_DOUBLE_SMALL_NUMBER)
		{
			return LongestSide;
		}
	}

	return DefaultVoxelMeshSizeCm;
}

FVector ACubelithPuzzleActor::GetInstanceScale() const
{
	// メッシュの 1 辺をボクセル 1 マス分にしてから、境目が見えるよう VoxelFillRatio だけ縮める
	const double Scale = static_cast<double>(VoxelFillRatio) * Cubelith::VoxelSizeCm / GetVoxelMeshSizeCm();
	return FVector(Scale, Scale, Scale);
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

int32 ACubelithPuzzleActor::FindPieceIdByComponent(const UPrimitiveComponent* Component) const
{
	if (Component == nullptr)
	{
		return INDEX_NONE;
	}

	// ピースは多くても 27 個（RULES.md 3.1 の M の上限）なので、逆引きの表は持たずに総当たりで足りる
	for (const TPair<int32, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : PieceMeshes)
	{
		if (Pair.Value.Get() == Component)
		{
			return Pair.Key;
		}
	}

	return INDEX_NONE;
}

FVector ACubelithPuzzleActor::GridToWorldLocation(const Cubelith::FVec3& Voxel) const
{
	// UpdatePlacements がインスタンスを置くのと同じ計算（アクタのローカル）をしてから、アクタの変換でワールドへ出す。
	// 今はワールド原点に置いているので変換は恒等だが、アクタを動かしても合うようにしてある
	const FVector Local = Cubelith::VoxelToWorld(Voxel) + SolutionSpaceCenterOffset(SpaceSize);
	return GetActorTransform().TransformPosition(Local);
}

void ACubelithPuzzleActor::SetSelectedPiece(int32 PieceId)
{
	if (SelectedPieceId == PieceId)
	{
		return;
	}

	const int32 PreviousPieceId = SelectedPieceId;
	SelectedPieceId = PieceId;

	// 先に前の選択を元の色へ戻してから新しい選択を強調する（同じピースを跨ぐことは無いが順は明確にしておく）
	if (PreviousPieceId != INDEX_NONE)
	{
		ApplyPieceColor(PreviousPieceId);
	}
	if (SelectedPieceId != INDEX_NONE)
	{
		ApplyPieceColor(SelectedPieceId);
	}
}

void ACubelithPuzzleActor::ApplyPieceColor(int32 PieceId)
{
	const FLinearColor* BaseColor = PieceBaseColors.Find(PieceId);
	TObjectPtr<UMaterialInstanceDynamic>* Found = PieceMaterials.Find(PieceId);
	UMaterialInstanceDynamic* PieceMaterial = (Found != nullptr) ? Found->Get() : nullptr;
	if (BaseColor == nullptr || PieceMaterial == nullptr)
	{
		// VoxelMaterial が空（Build で警告済み）か、未知の id。色を変える手立てが無いので何もしない
		return;
	}

	FLinearColor Color = *BaseColor;
	if (PieceId == SelectedPieceId)
	{
		// 仮の強調（U5 で縁取り・発光に置き換える）: 白へ寄せてから明るくする。
		// 乗算はアルファにも掛かるので、元のアルファに戻してから流す
		Color = FMath::Lerp(Color, FLinearColor::White, FMath::Clamp(SelectionWhitenAmount, 0.0f, 1.0f));
		Color *= FMath::Max(0.0f, SelectionBrightnessScale);
		Color.A = BaseColor->A;
	}
	else if (PieceId == SnapHintPieceId)
	{
		// スナップ候補の仮の発光（RULES.md 5.1。U5 でマテリアルの Emissive に置き換える）。
		// **選択が優先**（上の分岐が先に効く）。選択中のピースは白へ寄って既に目立っているので、
		// そこへ弱い持ち上げを重ねても見分けが付かず、「候補が出た」ことが伝わらない
		Color *= FMath::Max(0.0f, SnapHintBrightnessScale);
		Color.A = BaseColor->A;
	}

	PieceMaterial->SetVectorParameterValue(ColorParameterName, Color);
}
