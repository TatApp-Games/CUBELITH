// ピース選択の実装。移植元は WebMock/src/input/pieceInput.ts（pickPiece / setSelected）

#include "CubelithPlayerController.h"

#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "WorldCollision.h"

#include "CubelithGameMode.h"
#include "CubelithLog.h"
#include "CubelithPuzzleActor.h"
#include "Game.h"

ACubelithPlayerController::ACubelithPlayerController()
{
	// マウスの位置は GetMousePosition で読む。カーソルが出ていない（= ビューポートが掴んだまま）と
	// 位置が取れないので、エディタやデスクトップで狙って押せるようカーソルを出しておく。
	// 実機（タッチ）では関係が無い。人が切りたければエディタのクラス既定値で外せる
	bShowMouseCursor = true;

	// クリック / タッチのイベント（bEnableClickEvents 等）は使わない。ピックは自前のライントレースで行う
	// （近接ピックのサンプル点を撃つ必要があり、エンジンのクリック判定では中心 1 本しか撃てないため）
}

void ACubelithPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 半径ごとに固定なので 1 度だけ作って使い回す（pieceInput.ts の TOUCH_PICK_OFFSETS / PRECISE_PICK_OFFSETS）
	EnsurePickOffsets();
}

void ACubelithPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	PollPointerPress();
}

void ACubelithPlayerController::PollPointerPress()
{
	// Enhanced Input の InputAction / InputMappingContext は .uasset なので使わず、生の入力状態を毎フレーム読む
	// （Docs/SPEC_UE.md 0 章）。入力マッピング（Config/DefaultInput.ini）にも依存しない
	double TouchX = 0.0;
	double TouchY = 0.0;
	bool bTouchDown = false;
	GetInputTouchState(ETouchIndex::Touch1, TouchX, TouchY, bTouchDown);

	// 押している間ずっとピックし直さないよう、押下の立ち上がりだけを見る
	if (bTouchDown && !bWasTouchDown)
	{
		HandlePointerPressed(FVector2D(TouchX, TouchY), /*bTouch=*/true);
	}
	bWasTouchDown = bTouchDown;

	const bool bMouseDown = IsInputKeyDown(EKeys::LeftMouseButton);
	// タッチが左クリックとしても流れてくる環境（Use Mouse for Touch）で二重に拾わない
	// （ACubelithOrbitPawn::PollInput が旋回で同じ手当てをしているのと同じ理由）
	if (!bTouchDown && bMouseDown && !bWasMouseDown)
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		if (GetMousePosition(MouseX, MouseY))
		{
			HandlePointerPressed(FVector2D(MouseX, MouseY), /*bTouch=*/false);
		}
	}
	bWasMouseDown = bMouseDown;
}

void ACubelithPlayerController::HandlePointerPressed(const FVector2D& ScreenPosition, bool bTouch)
{
	// 選択は「押した瞬間」に確定させる（pieceInput.ts は pointerdown で選ぶ）。離した位置は見ない
	const int32 PieceId = PickPieceAtScreenPosition(ScreenPosition, bTouch);
	if (PieceId == INDEX_NONE)
	{
		// 何も無い場所 → 選択は保ったまま（RULES.md 3.3）。ドラッグはカメラの旋回になる（003 で切り分ける）
		return;
	}

	SetSelectedPiece(PieceId);
}

void ACubelithPlayerController::SetSelectedPiece(int32 PieceId)
{
	if (SelectedPieceId == PieceId)
	{
		return;
	}

	SelectedPieceId = PieceId;

	// 見た目（前の選択を元の色へ戻す・新しい選択を強調する）はピースを描くアクタが受け持つ
	if (ACubelithPuzzleActor* PuzzleActor = GetPuzzleActor())
	{
		PuzzleActor->SetSelectedPiece(PieceId);
	}

	if (PieceId == INDEX_NONE)
	{
		UE_LOG(LogCubelith, Log, TEXT("ピースの選択を解除した"));
		return;
	}

	// 固定中のピースも選択だけはできる（RULES.md 3.3）。固定を付ける手段は U4 なので、
	// ここでは状態を読んでログに出すだけにして、判定の経路を通しておく
	const TCHAR* LockText = TEXT("なし");
	if (const ACubelithGameMode* GameMode = GetCubelithGameMode())
	{
		if (const Cubelith::FGame* Game = GameMode->GetGame())
		{
			const TOptional<Cubelith::ELockKind> LockKind = Game->LockKindOf(PieceId);
			if (LockKind.IsSet())
			{
				LockText = (LockKind.GetValue() == Cubelith::ELockKind::Hint) ? TEXT("ヒント") : TEXT("手動");
			}
		}
	}

	UE_LOG(LogCubelith, Log, TEXT("ピース %d を選択した（固定: %s）"), PieceId, LockText);
}

int32 ACubelithPlayerController::PickPieceAtScreenPosition(const FVector2D& ScreenPosition, bool bTouch)
{
	if (GetPuzzleActor() == nullptr)
	{
		return INDEX_NONE;
	}

	EnsurePickOffsets();
	// 解釈: pieceInput.ts は pointerType が 'touch' のときだけ広い半径にする。こちらも同じく
	// タッチかどうかだけで分け、それ以外（マウス）は狙いが正確な側へ倒す
	const TArray<Cubelith::FPickOffset>& Offsets = bTouch ? TouchPickOffsets : MousePickOffsets;

	int32 NearestPieceId = INDEX_NONE;
	double NearestDistance = TNumericLimits<double>::Max();

	for (int32 Index = 0; Index < Offsets.Num(); ++Index)
	{
		const Cubelith::FPickOffset& Offset = Offsets[Index];

		// FPickOffset の Dy は画面下向きが +。ビューポートの座標も下向きが + なのでそのまま足せる
		FVector WorldOrigin = FVector::ZeroVector;
		FVector WorldDirection = FVector::ZeroVector;
		if (!DeprojectScreenPositionToWorld(
				static_cast<float>(ScreenPosition.X + Offset.Dx),
				static_cast<float>(ScreenPosition.Y + Offset.Dy),
				WorldOrigin,
				WorldDirection))
		{
			continue;
		}

		double HitDistance = 0.0;
		const int32 PieceId = TraceNearestPiece(WorldOrigin, WorldDirection, HitDistance);
		if (PieceId == INDEX_NONE)
		{
			continue;
		}

		// 先頭は必ず中心のレイ（PickSampleOffsets の約束）。当たったならずらしたレイを撃つまでもない
		if (Index == 0)
		{
			return PieceId;
		}

		// 複数当たったときはレイ原点に最も近いものを選ぶ（重なっているときに手前を選ぶ中心レイと揃う）
		if (HitDistance < NearestDistance)
		{
			NearestDistance = HitDistance;
			NearestPieceId = PieceId;
		}
	}

	return NearestPieceId;
}

int32 ACubelithPlayerController::TraceNearestPiece(
	const FVector& WorldOrigin, const FVector& WorldDirection, double& OutDistance) const
{
	OutDistance = 0.0;

	const UWorld* World = GetWorld();
	ACubelithPuzzleActor* PuzzleActor = GetPuzzleActor();
	if (World == nullptr || PuzzleActor == nullptr)
	{
		return INDEX_NONE;
	}

	const FVector TraceEnd = WorldOrigin + WorldDirection * FMath::Max(1.0, PickTraceDistanceCm);

	// 物理は使わない（RULES.md 3.4）ので、当たり判定はクエリ専用。ISM の簡易コリジョンを撃つだけなので
	// bTraceComplex は false（ACubelithPuzzleActor がボクセルのコリジョンをクエリ専用で用意している）
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CubelithPickPiece), /*bInTraceComplex=*/false);
	QueryParams.bReturnPhysicalMaterial = false;

	TArray<FHitResult> Hits;
	if (!World->LineTraceMultiByChannel(Hits, WorldOrigin, TraceEnd, ECC_Visibility, QueryParams))
	{
		return INDEX_NONE;
	}

	// LineTraceMultiByChannel は手前から並べて返す。最初に見つかったピースを採る
	for (const FHitResult& Hit : Hits)
	{
		const int32 PieceId = PuzzleActor->FindPieceIdByComponent(Hit.GetComponent());
		if (PieceId != INDEX_NONE)
		{
			OutDistance = Hit.Distance;
			return PieceId;
		}
	}

	return INDEX_NONE;
}

void ACubelithPlayerController::EnsurePickOffsets()
{
	const double Scale = GetPickRadiusScale();
	if (PickOffsetsScale >= 0.0 && FMath::IsNearlyEqual(Scale, PickOffsetsScale))
	{
		return;
	}

	TouchPickOffsets = Cubelith::PickSampleOffsets(FMath::Max(0.0, TouchPickRadiusPx) * Scale);
	MousePickOffsets = Cubelith::PickSampleOffsets(FMath::Max(0.0, MousePickRadiusPx) * Scale);
	PickOffsetsScale = Scale;
}

double ACubelithPlayerController::GetPickRadiusScale() const
{
	if (!bScalePickRadiusByDpi)
	{
		return 1.0;
	}

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	if (ViewportX <= 0 || ViewportY <= 0)
	{
		return 1.0;
	}

	// UMG が使うのと同じ DPI スケール。CSS ピクセル（端末非依存）で決めた半径を
	// ビューポートのピクセル（DeprojectScreenPositionToWorld とタッチ座標の単位）へ直す
	const float DpiScale =
		GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(ViewportX, ViewportY));
	return (DpiScale > 0.0f) ? static_cast<double>(DpiScale) : 1.0;
}

ACubelithGameMode* ACubelithPlayerController::GetCubelithGameMode() const
{
	const UWorld* World = GetWorld();
	return (World != nullptr) ? Cast<ACubelithGameMode>(World->GetAuthGameMode()) : nullptr;
}

ACubelithPuzzleActor* ACubelithPlayerController::GetPuzzleActor() const
{
	const ACubelithGameMode* GameMode = GetCubelithGameMode();
	return (GameMode != nullptr) ? GameMode->GetPuzzleActor() : nullptr;
}
