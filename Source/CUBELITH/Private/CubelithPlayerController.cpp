// ピース選択とドラッグ移動の実装。移植元は WebMock/src/input/pieceInput.ts
// （pickPiece / setSelected / MoveDrag / currentAxes / pixelsPerVoxelAt / orbit.enabled の扱い）

#include "CubelithPlayerController.h"

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "WorldCollision.h"

#include "CubelithCoords.h"
#include "CubelithGameMode.h"
#include "CubelithLog.h"
#include "CubelithOrbitPawn.h"
#include "CubelithPuzzleActor.h"
#include "Game.h"

namespace
{
	/**
	 * UE のワールド方向をロジック座標の方向へ直す（Docs/SPEC_UE.md 7.2 の置換の逆）。
	 * UE の (X, Y, Z) = (x, z, y) なので、logic = (UE.X, UE.Z, UE.Y)。y と z の入れ替えなので自分自身が逆変換になる
	 */
	FVector WorldToLogicDirection(const FVector& WorldDirection)
	{
		return FVector(WorldDirection.X, WorldDirection.Z, WorldDirection.Y);
	}
}

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

	// 未選択なので旋回は有効。Pawn がまだ湧いていなくても bOrbitEnabled の既定値が同じなので困らない
	UpdateOrbitEnabled();
}

void ACubelithPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	PollPointer();
}

void ACubelithPlayerController::PollPointer()
{
	// Enhanced Input の InputAction / InputMappingContext は .uasset なので使わず、生の入力状態を毎フレーム読む
	// （Docs/SPEC_UE.md 0 章）。入力マッピング（Config/DefaultInput.ini）にも依存しない
	double Touch1X = 0.0;
	double Touch1Y = 0.0;
	double Touch2X = 0.0;
	double Touch2Y = 0.0;
	bool bTouch1Down = false;
	bool bTouch2Down = false;
	GetInputTouchState(ETouchIndex::Touch1, Touch1X, Touch1Y, bTouch1Down);
	GetInputTouchState(ETouchIndex::Touch2, Touch2X, Touch2Y, bTouch2Down);

	if (bTouch2Down && DragMode == ECubelithDragMode::MovePiece)
	{
		// 指が 2 本になったらドラッグ移動は打ち切る（2 本指の割り当ては 005。pieceInput.ts の onPointerDown が
		// 2 本目で drag = null にするのと同じ）。旋回の有効・無効はここでは触らない
		// ＝ このドラッグの役割は「ピース操作」のままで、離すまで変えない
		DragMode = ECubelithDragMode::None;
		DragPieceId = INDEX_NONE;
	}

	// タッチ 1 本目。押した瞬間 / 押しながら動かした / 離した、をポーリングの差分から作る
	const FVector2D Touch1Position(Touch1X, Touch1Y);
	if (bTouch1Down && !bWasTouchDown)
	{
		// 既に 2 本目が触れているところへ 1 本目が来ることは無い（Touch1 が先に埋まる）が、念のため弾く
		if (!bTouch2Down)
		{
			HandlePointerPressed(Touch1Position, /*bTouch=*/true);
		}
	}
	else if (bTouch1Down && bWasTouchDown)
	{
		HandlePointerMoved(Touch1Position);
	}
	else if (!bTouch1Down && bWasTouchDown)
	{
		HandlePointerReleased();
	}
	bWasTouchDown = bTouch1Down;

	const bool bMouseDown = IsInputKeyDown(EKeys::LeftMouseButton);
	// タッチが左クリックとしても流れてくる環境（Use Mouse for Touch）で二重に拾わない
	// （ACubelithOrbitPawn::PollInput が旋回で同じ手当てをしているのと同じ理由）
	if (!bTouch1Down && !bTouch2Down)
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		// カーソルがビューポートの外にあるなど、位置が取れないフレームは何もしない（状態は保つ）
		const bool bHasMousePosition = GetMousePosition(MouseX, MouseY);
		const FVector2D MousePosition(MouseX, MouseY);

		if (bMouseDown && !bWasMouseDown)
		{
			if (bHasMousePosition)
			{
				HandlePointerPressed(MousePosition, /*bTouch=*/false);
			}
		}
		else if (bMouseDown && bWasMouseDown)
		{
			if (bHasMousePosition)
			{
				HandlePointerMoved(MousePosition);
			}
		}
		else if (!bMouseDown && bWasMouseDown)
		{
			HandlePointerReleased();
		}
	}
	bWasMouseDown = bMouseDown;
}

void ACubelithPlayerController::HandlePointerPressed(const FVector2D& ScreenPosition, bool bTouch)
{
	if (DragMode != ECubelithDragMode::None)
	{
		// 2 つ目のポインタ（別の指や、タッチ中に流れてきたマウス）。進行中のドラッグを畳むだけで、
		// 新しいドラッグは始めない（役割は 1 本目のポインタで決まる。2 本指の割り当ては 005）
		EndDrag();
		return;
	}

	// 選択は「押した瞬間」に確定させる（pieceInput.ts は pointerdown で選ぶ）。離した位置は見ない
	const int32 PieceId = PickPieceAtScreenPosition(ScreenPosition, bTouch);
	if (PieceId == INDEX_NONE)
	{
		// 何も無い場所 → 選択は保ったまま、このドラッグの間だけカメラを旋回させる（RULES.md 3.3「カメラ」）
		DragMode = ECubelithDragMode::OrbitCamera;
		DragPieceId = INDEX_NONE;
		UpdateOrbitEnabled();
		return;
	}

	SetSelectedPiece(PieceId);

	// 固定中のピースは選ぶだけで動かさない（RULES.md 3.3「固定」）。カメラの旋回にも回さない
	// （ピースを押しているので、このドラッグはピース操作の側）
	if (IsPieceLocked(PieceId))
	{
		return;
	}

	BeginMoveDrag(PieceId, ScreenPosition);
}

void ACubelithPlayerController::HandlePointerMoved(const FVector2D& ScreenPosition)
{
	if (DragMode != ECubelithDragMode::MovePiece)
	{
		// カメラの旋回は ACubelithOrbitPawn が自分で入力を読む。ここでは何もしない
		return;
	}

	const double PixelsPerVoxel = FMath::Max(DragPixelsPerVoxel, UE_DOUBLE_KINDA_SMALL_NUMBER);
	const double Dx = ScreenPosition.X - DragStartScreenPosition.X;
	const double Dy = ScreenPosition.Y - DragStartScreenPosition.Y;

	// 画面座標の Y は下向きが正なので、上へのドラッグが +Up になるよう符号を反転する
	// （ACubelithOrbitPawn がマウスの「デルタ」の Y を反転しているのとは別の話。こちらは座標の向き）。
	// 四捨五入なので半マス分ドラッグするまでは動かない = そのままクリックの遊びになる
	const int32 StepsRight = FMath::RoundToInt32(Dx / PixelsPerVoxel);
	const int32 StepsUp = FMath::RoundToInt32(-Dy / PixelsPerVoxel);

	// 前回まで反映した分との差だけを流す（同じマスに留まっている間は Move を呼ばない）
	const int32 DeltaRight = StepsRight - DragAppliedRight;
	const int32 DeltaUp = StepsUp - DragAppliedUp;
	if (DeltaRight == 0 && DeltaUp == 0)
	{
		return;
	}

	Cubelith::FGame* Game = GetGame();
	if (Game == nullptr)
	{
		return;
	}

	DragAppliedRight = StepsRight;
	DragAppliedUp = StepsUp;

	// 重なりは許す（RULES.md 3.3「操作中はピース同士が重なってもよい」）ので、Move はそのまま通す。
	// 配置が変われば FGame の OnChange から ACubelithPuzzleActor::UpdatePlacements が呼ばれる
	Game->Move(DragPieceId, Cubelith::AddVec3(
		Cubelith::AxisStepVector(ActiveDragAxes.Right, DeltaRight),
		Cubelith::AxisStepVector(ActiveDragAxes.Up, DeltaUp)));
}

void ACubelithPlayerController::HandlePointerReleased()
{
	// マグネット・スナップ（RULES.md 3.5）は U4 でここに入る
	EndDrag();
}

void ACubelithPlayerController::BeginMoveDrag(int32 PieceId, const FVector2D& ScreenPosition)
{
	// ドラッグ軸と感度はこの時点で固定する。途中でカメラが動いても写像と 1 マスの長さがぶれない
	// （pieceInput.ts の MoveDrag。選択中は旋回を止めるのでそもそも動かないが、慣性の減衰は残っている）
	Cubelith::FDragAxes Axes;
	if (!ComputeDragAxes(Axes))
	{
		// カメラが取れないと画面の右 / 上がどのグリッド軸に当たるか決められない。選択だけ残して動かさない
		UE_LOG(LogCubelith, Warning, TEXT("カメラが取れないのでピース %d のドラッグ移動を始められない"), PieceId);
		return;
	}

	DragMode = ECubelithDragMode::MovePiece;
	DragPieceId = PieceId;
	DragStartScreenPosition = ScreenPosition;
	ActiveDragAxes = Axes;
	DragPixelsPerVoxel = PixelsPerVoxelAt(PieceId);
	DragAppliedRight = 0;
	DragAppliedUp = 0;
}

void ACubelithPlayerController::EndDrag()
{
	if (DragMode == ECubelithDragMode::None)
	{
		return;
	}

	DragMode = ECubelithDragMode::None;
	DragPieceId = INDEX_NONE;
	DragAppliedRight = 0;
	DragAppliedUp = 0;

	// カメラの旋回だったなら「選択中は止める」状態へ戻す。選択はそのまま残る（RULES.md 3.3）
	UpdateOrbitEnabled();
}

bool ACubelithPlayerController::GetCameraBasis(FVector& OutRight, FVector& OutUp, FVector& OutForward) const
{
	if (PlayerCameraManager == nullptr)
	{
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	// PlayerCameraManager が持つ今の視点（= ACubelithOrbitPawn のカメラ）
	GetPlayerViewPoint(ViewLocation, ViewRotation);

	// UE のカメラは自分の +X を向き、+Y が右・+Z が上（EAxis は UE の側。Cubelith::EAxis とは別物）
	const FRotationMatrix Basis(ViewRotation);
	OutForward = Basis.GetUnitAxis(::EAxis::X);
	OutRight = Basis.GetUnitAxis(::EAxis::Y);
	OutUp = Basis.GetUnitAxis(::EAxis::Z);
	return true;
}

bool ACubelithPlayerController::ComputeDragAxes(Cubelith::FDragAxes& OutAxes) const
{
	FVector WorldRight = FVector::ZeroVector;
	FVector WorldUp = FVector::ZeroVector;
	FVector WorldForward = FVector::ZeroVector;
	if (!GetCameraBasis(WorldRight, WorldUp, WorldForward))
	{
		return false;
	}

	// 解釈: ACubelithPuzzleActor は平行移動しか持たないので、ワールド軸とグリッド軸は向きが一致する
	// （pieceInput.ts の currentAxes と同じ前提）。あとは 7.2 の置換でロジック座標へ直せばよい
	OutAxes = Cubelith::DragAxes(
		WorldToLogicDirection(WorldRight),
		WorldToLogicDirection(WorldUp),
		WorldToLogicDirection(WorldForward));
	return true;
}

double ACubelithPlayerController::PixelsPerVoxelAt(int32 PieceId) const
{
	const double Lower = FMath::Max(1.0, MinPixelsPerVoxel);
	const double Upper = FMath::Max(Lower, MaxPixelsPerVoxel);

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	if (ViewportX <= 0 || ViewportY <= 0 || PlayerCameraManager == nullptr)
	{
		// 測れないときは一番細かい感度に倒す（pieceInput.ts が高さ 0 のとき下限に張り付くのと同じ）
		return Lower;
	}

	// ピースの位置はグリッド座標を実際に描いているワールド座標へ直して使う。取れなければ解答空間の中心で代用する
	FVector PieceLocation = FVector::ZeroVector;
	if (const ACubelithPuzzleActor* PuzzleActor = GetPuzzleActor())
	{
		PieceLocation = PuzzleActor->GetActorLocation();
		if (const Cubelith::FGame* Game = GetGame())
		{
			if (const Cubelith::FPlacement* Placement = Game->PlacementOf(PieceId))
			{
				PieceLocation = PuzzleActor->GridToWorldLocation(Placement->Position);
			}
		}
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const double Distance = FMath::Max(FVector::Dist(ViewLocation, PieceLocation), UE_DOUBLE_KINDA_SMALL_NUMBER);

	// UE の視野角は垂直画角（エンジン既定の AspectRatioAxisConstraint = MaintainYFOV。縦横比で変わるのは
	// 水平側だけ）。camera.ts の PerspectiveCamera.fov も垂直画角なので、pixelsPerVoxelAt の式に
	// ビューポートの高さと組にしてそのまま入れられる
	const double HalfFovRad =
		FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(PlayerCameraManager->GetFOVAngle()), 1.0, 170.0) * 0.5);
	const double TanHalfVerticalFov = FMath::Tan(HalfFovRad);
	if (TanHalfVerticalFov <= UE_DOUBLE_SMALL_NUMBER)
	{
		return Lower;
	}

	// pieceInput.ts の pixelsPerVoxelAt。あちらは 1 ボクセル = 1 単位なので掛けていないが、UE は cm なので
	// ボクセル 1 マスぶん（Cubelith::VoxelSizeCm）を掛けて「1 マスの画面上の長さ」にする
	const double Pixels =
		static_cast<double>(ViewportY) * Cubelith::VoxelSizeCm / (2.0 * Distance * TanHalfVerticalFov);
	return FMath::Clamp(Pixels, Lower, Upper);
}

bool ACubelithPlayerController::IsPieceLocked(int32 PieceId) const
{
	const Cubelith::FGame* Game = GetGame();
	return (Game != nullptr) && Game->LockKindOf(PieceId).IsSet();
}

void ACubelithPlayerController::UpdateOrbitEnabled()
{
	ACubelithOrbitPawn* OrbitPawn = GetOrbitPawn();
	if (OrbitPawn == nullptr)
	{
		return;
	}

	// 選択中はドラッグをピース操作に回すので旋回を止める（pieceInput.ts の setSelected）。
	// ただし何も無い場所から始めたドラッグの間だけは、選択中でも旋回させる（RULES.md 3.3「カメラ」）。
	// ズームはここでは止まらない（ACubelithOrbitPawn が bOrbitEnabled と無関係に効かせる）
	OrbitPawn->bOrbitEnabled =
		(DragMode == ECubelithDragMode::OrbitCamera) || (SelectedPieceId == INDEX_NONE);
}

Cubelith::FGame* ACubelithPlayerController::GetGame() const
{
	const ACubelithGameMode* GameMode = GetCubelithGameMode();
	return (GameMode != nullptr) ? GameMode->GetGame() : nullptr;
}

ACubelithOrbitPawn* ACubelithPlayerController::GetOrbitPawn() const
{
	return Cast<ACubelithOrbitPawn>(GetPawn());
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

	// 選択中はドラッグをピース操作に回す（pieceInput.ts の setSelected が orbit.enabled を切り替えるのと同じ）
	UpdateOrbitEnabled();

	if (PieceId == INDEX_NONE)
	{
		UE_LOG(LogCubelith, Log, TEXT("ピースの選択を解除した"));
		return;
	}

	// 固定中のピースも選択だけはできる（RULES.md 3.3）。固定を付ける手段は U4 なので、
	// ここでは状態を読んでログに出すだけにして、判定の経路を通しておく
	const TCHAR* LockText = TEXT("なし");
	if (const Cubelith::FGame* Game = GetGame())
	{
		const TOptional<Cubelith::ELockKind> LockKind = Game->LockKindOf(PieceId);
		if (LockKind.IsSet())
		{
			LockText = (LockKind.GetValue() == Cubelith::ELockKind::Hint) ? TEXT("ヒント") : TEXT("手動");
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
