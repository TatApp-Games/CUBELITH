// ピース選択・ドラッグ移動・90 度回転・マグネットスナップの実装。移植元は WebMock/src/input/pieceInput.ts
// （pickPiece / setSelected / MoveDrag / currentAxes / pixelsPerVoxelAt / orbit.enabled の扱い、
//   applyTwoFinger / RotateDrag / updateRotateDrag / commitRotateDrag）と、
// スナップの配線は WebMock/src/main.ts（snap.refresh / snap.release / onSnap / snapMotion の掛け外し）

#include "CubelithPlayerController.h"

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "WorldCollision.h"

#include "CubelithCoords.h"
#include "CubelithFreeRotation.h"
#include "CubelithGameMode.h"
#include "CubelithLog.h"
#include "CubelithOrbitPawn.h"
#include "CubelithPuzzleActor.h"
#include "CubelithSnapControl.h"
#include "CubelithSnapMotion.h"
#include "Game.h"
#include "Grid.h"

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

	/** ログに出すグリッド軸の名前（ロジック座標の x / y / z） */
	const TCHAR* LogicAxisName(Cubelith::EAxis Axis)
	{
		switch (Axis)
		{
		case Cubelith::EAxis::X: return TEXT("x");
		case Cubelith::EAxis::Y: return TEXT("y");
		default: return TEXT("z");
		}
	}

	/** ログに出す 2 本指ジェスチャの名前 */
	const TCHAR* TwoFingerGestureName(Cubelith::ETwoFingerRotateGesture Gesture)
	{
		switch (Gesture)
		{
		case Cubelith::ETwoFingerRotateGesture::Yaw: return TEXT("左右のスワイプ");
		case Cubelith::ETwoFingerRotateGesture::Pitch: return TEXT("上下のスワイプ");
		default: return TEXT("ひねり");
		}
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

	// 画面（UMG）のボタンとゲームの操作を同時に効かせる（Docs/SPEC_UE.md 4 章の UI）。
	// 既定の「ゲームだけ」だとビューポートがマウスを掴んだままになり、タイトルのボタンを押せないことがある。
	// 掴むのは押している間だけ（FInputModeGameAndUI の既定）なので、カメラの旋回とピースのドラッグはそのまま通る。
	// カーソルは掴んでいる間も出したまま（ドラッグ中に消えると狙いを付け直せない）
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	// 未選択なので旋回は有効。Pawn がまだ湧いていなくても bOrbitEnabled の既定値が同じなので困らない
	UpdateOrbitEnabled();

	// 人がエディタで変えた補間の時間を反映する（走っている補間にもそのまま効く）
	SnapMotion.SetDurationSeconds(FMath::Max(0.0, SnapDurationSeconds));

	// 配置が変わったら候補を計算し直す（main.ts が game の onChange で snap.refresh を呼ぶのと同じ場所）。
	// AddUObject なのでこのコントローラが消えたら自動で飛ばされる ＝ 明示的な解除は要らない
	if (ACubelithGameMode* GameMode = GetCubelithGameMode())
	{
		GameMode->OnPlacementsChanged.AddUObject(this, &ACubelithPlayerController::HandlePlacementsChanged);
	}

	// パズルが既に開いていれば初期状態の候補を出す（開いていなければ最初の配置の変化で出る）
	RefreshSnapHint();
}

void ACubelithPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	PollPointer();

	// スナップの補間はポーリング入力と同じ場所で進める（main.ts の session.update と同じ役目）
	UpdateSnapMotion();
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
		// 指が 2 本になったらドラッグ移動は打ち切る（pieceInput.ts の onPointerDown が
		// 2 本目で drag = null にするのと同じ）。旋回の有効・無効はここでは触らない
		// ＝ このドラッグの役割は「ピース操作」のままで、離すまで変えない
		DragMode = ECubelithDragMode::None;
		DragPieceId = INDEX_NONE;
	}

	const FVector2D Touch1Position(Touch1X, Touch1Y);
	const FVector2D Touch2Position(Touch2X, Touch2Y);

	// 選択中のピースがある回転ありの盤面では、2 本指をこちらが乗っ取って
	// 「90 度回転」と「ピンチのズーム」に振り分ける（pieceInput.ts の onPointerDown の 2 本目の分岐）。
	// 回転「なし」と未選択のときは乗っ取らない ＝ 2 本指はそのまま Pawn のピンチズームになる（RULES.md 3.1）
	const bool bConsumeTwoFinger =
		bTouch1Down && bTouch2Down && SelectedPieceId != INDEX_NONE && IsRotationAllowed();
	if (bConsumeTwoFinger)
	{
		if (!bTwoFingerActive)
		{
			// 走っているドラッグは畳む（回転モードの自由回転はその場で確定させる。commitRotateDrag と同じ）。
			// 何も無い場所から始めたカメラの旋回も、ここで「選択中は止める」状態へ戻る
			EndDrag();

			bTwoFingerActive = true;
			Gesture.Reset(Touch1Position, Touch2Position);

			// 同じピンチが Pawn 側とジェスチャ側で二重に効かないよう、Pawn のタッチのピンチを止める。
			// 解釈: 003 でズームは選択の有無によらず効くようにしたので、どちらか一方に寄せる必要がある。
			// 寄せ先はコントローラ（= ジェスチャの判定）にした。twoFingerGesture.ts の状態機械が
			// 「ピンチか回転か」を最初に超えた閾値で決め打つので、判定をそこ 1 か所に集めた方が
			// 「回そうとしたのに少し寄る」が起きない
			if (ACubelithOrbitPawn* OrbitPawn = GetOrbitPawn())
			{
				OrbitPawn->bTouchPinchEnabled = false;
			}
		}
		else
		{
			ApplyTwoFinger(Touch1Position, Touch2Position);
		}
	}
	else if (bTwoFingerActive)
	{
		// 指が 1 本ずつ離れる（片方だけ離れた）ときもここに来る。残った指でドラッグ移動が始まることは無い
		// （押した瞬間の立ち上がりが来ないので DragMode は None のまま。pieceInput.ts で drag が null のままなのと同じ）
		bTwoFingerActive = false;
		if (ACubelithOrbitPawn* OrbitPawn = GetOrbitPawn())
		{
			OrbitPawn->bTouchPinchEnabled = true;
		}
	}

	// タッチ 1 本目。押した瞬間 / 押しながら動かした / 離した、をポーリングの差分から作る
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

	// マウスは左ボタンだけを見る（回転は回転モード中の同じドラッグで行う。U3 で仮に使っていた
	// 右ボタンのドラッグは HUD の回転モードのトグルへ置き換えたので、右ボタンには何も紐づかない）
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

	if (bRotateModeOn)
	{
		// 回転モード中のドラッグは移動ではなく自由回転（RULES.md 3.3）。別のピースを掴んだなら
		// 直前の SetSelectedPiece が回転モードを抜けているので、ここには来ない
		BeginRotateDrag(PieceId, ScreenPosition);
		return;
	}

	BeginMoveDrag(PieceId, ScreenPosition);
}

void ACubelithPlayerController::HandlePointerMoved(const FVector2D& ScreenPosition)
{
	if (DragMode == ECubelithDragMode::RotatePiece)
	{
		// 回転モード中のドラッグ（90 度に縛らず見せるだけ。確定は離した時点）
		UpdateRotateDrag(ScreenPosition);
		return;
	}

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

	// 手で動かしたら前のスナップの補間は用済み（main.ts の onMove が snapMotion.cancel を呼ぶのと同じ）
	CancelSnapMotion(DragPieceId);

	// 重なりは許す（RULES.md 3.3「操作中はピース同士が重なってもよい」）ので、Move はそのまま通す。
	// 配置が変われば FGame の OnChange から ACubelithPuzzleActor::UpdatePlacements が呼ばれる
	Game->Move(DragPieceId, Cubelith::AddVec3(
		Cubelith::AxisStepVector(ActiveDragAxes.Right, DeltaRight),
		Cubelith::AxisStepVector(ActiveDragAxes.Up, DeltaUp)));
}

void ACubelithPlayerController::HandlePointerReleased()
{
	// 吸着させるのは「そのピースを動かしていたドラッグ」だけ。カメラの旋回では何も吸い付かず、
	// 回転モード中のドラッグは EndDrag の中で向きの確定に回る（pieceInput.ts が回転のときは
	// onRelease を呼ばないのと同じ）。EndDrag が状態を畳むので、先に控えておく
	const int32 ReleasedPieceId = (DragMode == ECubelithDragMode::MovePiece) ? DragPieceId : INDEX_NONE;

	EndDrag();

	// マグネット・スナップ（RULES.md 3.5）。指 / ボタンを離したこの時点で吸着させる
	ApplySnapOnRelease(ReleasedPieceId);
}

void ACubelithPlayerController::ApplySnapOnRelease(int32 PieceId)
{
	if (PieceId == INDEX_NONE)
	{
		return;
	}

	Cubelith::FGame* Game = GetGame();
	if (Game == nullptr)
	{
		return;
	}

	// 固定中のピースは吸い付かない（RULES.md 3.3。吸い付いて動いてしまわないように。
	// pieceInput.ts が onRelease を isLocked で弾いているのと同じ）
	if (IsPieceLocked(PieceId))
	{
		return;
	}

	EnsureSnapControl();

	const TOptional<Cubelith::FSnapTarget> Target = SnapControl.Release(Game->Placements(), PieceId);

	// Release は吸着の有無によらず発光を消す。その結果を見た目へ反映する
	if (ACubelithPuzzleActor* PuzzleActor = GetPuzzleActor())
	{
		PuzzleActor->SetSnapHint(SnapControl.HintedPieceId());
	}

	if (!Target.IsSet())
	{
		return;
	}

	// ポインタが指す先は下の Move で無効になるので、必要な値を先に写す
	const Cubelith::FVec3 FromPosition = Target->From.Position;
	const Cubelith::FVec3 ToPosition = Target->To.Position;

	// 論理上の配置は整数座標のまま即座に確定させる（ここで OnChange が回り、描画と候補の再計算が走る）。
	// 向きは変えない（Solve.h の SnapCandidate は位置だけを動かす）ので Move で足りる
	Game->Move(PieceId, Cubelith::SubVec3(ToPosition, FromPosition));

	// 見た目だけを 100〜150 ms かけて追いつかせる。「移動前 − 移動後」から 0 へ（snapMotion.ts の start）
	SnapMotion.Start(PieceId, Cubelith::SubVec3(FromPosition, ToPosition),
		(GetWorld() != nullptr) ? GetWorld()->GetTimeSeconds() : 0.0, SnapOffsetBuffer);
	ApplySnapOffsets();

	// 吸い付いた瞬間に鳴らす（RULES.md 3.5）。音が割り当てられていなければ鳴らない
	if (ACubelithGameMode* GameMode = GetCubelithGameMode())
	{
		GameMode->PlaySnapSound();
	}

	UE_LOG(LogCubelith, Verbose, TEXT("ピース %d を吸着させた: (%d, %d, %d) → (%d, %d, %d)"),
		PieceId, FromPosition.X, FromPosition.Y, FromPosition.Z, ToPosition.X, ToPosition.Y, ToPosition.Z);
}

void ACubelithPlayerController::RefreshSnapHint()
{
	const Cubelith::FGame* Game = GetGame();
	if (Game == nullptr)
	{
		return;
	}

	EnsureSnapControl();

	// 固定中のピースは吸い付かないので候補も出さない（main.ts が lockKindOf を見て null を渡すのと同じ）
	const int32 ActivePieceId =
		(SelectedPieceId != INDEX_NONE && !IsPieceLocked(SelectedPieceId)) ? SelectedPieceId : INDEX_NONE;

	// 光らせる対象が変わったときだけ見た目へ流す（FSnapControl が変化を見てくれる）
	if (!SnapControl.Refresh(Game->Placements(), ActivePieceId))
	{
		return;
	}

	if (ACubelithPuzzleActor* PuzzleActor = GetPuzzleActor())
	{
		PuzzleActor->SetSnapHint(SnapControl.HintedPieceId());
	}
}

void ACubelithPlayerController::HandlePlacementsChanged(TArrayView<const Cubelith::FPlacement> Placements)
{
	// 配置が変わったこのタイミングだけで候補を計算し直す（毎フレームは回さない。RULES.md 5.1）。
	// 渡された配置ではなく FGame から読み直すのは、確定した後の状態を 1 か所から見るためで、
	// OnChange は配置を差し替えた後に呼ばれるので中身は同じ
	RefreshSnapHint();
}

void ACubelithPlayerController::CancelSnapMotion(int32 PieceId)
{
	if (PieceId == INDEX_NONE || !SnapMotion.IsRunning(PieceId))
	{
		return;
	}

	SnapMotion.Cancel(PieceId, SnapOffsetBuffer);
	ApplySnapOffsets();
}

void ACubelithPlayerController::UpdateSnapMotion()
{
	if (SnapMotion.Num() == 0)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	SnapMotion.Update(World->GetTimeSeconds(), SnapOffsetBuffer);
	ApplySnapOffsets();
}

void ACubelithPlayerController::ApplySnapOffsets()
{
	if (SnapOffsetBuffer.Num() == 0)
	{
		return;
	}

	ACubelithPuzzleActor* PuzzleActor = GetPuzzleActor();
	if (PuzzleActor == nullptr)
	{
		return;
	}

	for (const Cubelith::FSnapOffsetUpdate& Update : SnapOffsetBuffer)
	{
		if (Update.bCleared)
		{
			PuzzleActor->ClearViewOffset(Update.PieceId);
		}
		else
		{
			PuzzleActor->SetViewOffset(Update.PieceId, Update.Offset);
		}
	}
}

void ACubelithPlayerController::ResetForNewSession()
{
	// 走っている自由回転は確定させずに捨てる（書き戻す先の盤面ごと消えるため。
	// CommitRotateDrag を通すと消えた FGame を触りに行く）
	DragMode = ECubelithDragMode::None;
	DragPieceId = INDEX_NONE;
	DragAppliedRight = 0;
	DragAppliedUp = 0;
	FreeRotationQuat = FQuat::Identity;
	bTwoFingerActive = false;
	// 回転モードは選んでいたピースに紐づくので、盤面が入れ替わるときに落とす
	bRotateModeOn = false;

	// ポーリング入力の「前フレームの押下状態」も戻す（画面が切り替わった直後に
	// 押しっぱなしの指 / ボタンを立ち上がりとして拾わないように）
	bWasTouchDown = false;
	bWasMouseDown = false;

	SelectedPieceId = INDEX_NONE;

	// 次に使うときに新しい盤面のピースで作り直させる（ヘッダの「解釈:」のとおり形が変わる）
	SnapControlPieceCount = 0;
	// 走っている補間は打ち切る。表示上のずれを戻す相手（前の盤面のアクタ）はこの後すぐ消えるので流さない
	SnapMotion.CancelAll(SnapOffsetBuffer);
	SnapOffsetBuffer.Reset();

	// 未選択に戻したので軌道カメラの旋回を戻す
	UpdateOrbitEnabled();
}

void ACubelithPlayerController::CommitFreeRotation()
{
	// 掛かっていなければ CommitRotateDrag が自分で弾く（DragMode が RotatePiece 以外なら何もしない）
	CommitRotateDrag();
}

void ACubelithPlayerController::CancelSnapMotionFor(int32 PieceId)
{
	CancelSnapMotion(PieceId);
}

void ACubelithPlayerController::CancelAllSnapMotion()
{
	if (SnapMotion.Num() == 0)
	{
		return;
	}

	// 表示だけのずれは戻す（散らし直しではピースのアクタがそのまま残るので、
	// ずれたままの見た目が残らないようにする。ResetForNewSession はアクタごと消えるので流さない）
	SnapMotion.CancelAll(SnapOffsetBuffer);
	ApplySnapOffsets();
}

void ACubelithPlayerController::EnsureSnapControl()
{
	const Cubelith::FGame* Game = GetGame();
	if (Game == nullptr)
	{
		return;
	}

	// 同じ盤面なら作り直さない。ピース数が変わったらパズルが入れ替わっている（U4 の「次の問題」）
	if (SnapControlPieceCount == Game->Pieces().Num() && SnapControlPieceCount > 0)
	{
		return;
	}

	SnapControl = Cubelith::FSnapControl(Game->Pieces(), Game->N());
	SnapControlPieceCount = Game->Pieces().Num();

	// 前の盤面の補間が残っていても新しい盤面では意味が無い（見た目は次の UpdatePlacements で整う）
	SnapMotion.CancelAll(SnapOffsetBuffer);
	ApplySnapOffsets();
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

	// 自由回転の途中で終わるなら、そこまでの回転を最寄りの向きへ確定させてから畳む
	// （表示と論理がずれたまま残さない。pieceInput.ts の commitRotateDrag）
	CommitRotateDrag();

	DragMode = ECubelithDragMode::None;
	DragPieceId = INDEX_NONE;
	DragAppliedRight = 0;
	DragAppliedUp = 0;

	// カメラの旋回だったなら「選択中は止める」状態へ戻す。選択はそのまま残る（RULES.md 3.3）
	UpdateOrbitEnabled();
}

bool ACubelithPlayerController::SetRotateMode(bool bEnabled)
{
	// 入れる条件（pieceInput.ts の setRotateMode）: パズルの回転「あり」・ピースを選んでいる・
	// そのピースが固定されていない。どれかが欠ければ入らない（入れたかは戻り値で分かる）
	const bool bNext = bEnabled
		&& IsRotationAllowed()
		&& SelectedPieceId != INDEX_NONE
		&& !IsPieceLocked(SelectedPieceId);

	if (bNext == bRotateModeOn)
	{
		if (bEnabled && !bNext)
		{
			// 入れなかった理由を出す（HUD はボタンを隠す / 押せなくするが、経路を追えるようにしておく）
			UE_LOG(LogCubelith, Log,
				TEXT("回転モードに入れない（パズルの回転=%s, 選択=%d, 固定=%s。RULES.md 3.1 / 3.3）"),
				IsRotationAllowed() ? TEXT("あり") : TEXT("なし"), SelectedPieceId,
				(SelectedPieceId != INDEX_NONE && IsPieceLocked(SelectedPieceId)) ? TEXT("あり") : TEXT("なし"));
		}
		return bRotateModeOn;
	}

	// 抜けるときに回転のドラッグが残っていたら、そこまでの回転を確定させる
	// （表示だけねじれたピースを残さない。掛かっていなければ CommitRotateDrag が自分で弾く）
	CommitRotateDrag();

	bRotateModeOn = bNext;

	UE_LOG(LogCubelith, Log, TEXT("回転モードを%s（ピース %d。RULES.md 3.3）"),
		bRotateModeOn ? TEXT("入れた") : TEXT("抜けた"), SelectedPieceId);

	return bRotateModeOn;
}

bool ACubelithPlayerController::ToggleRotateMode()
{
	return SetRotateMode(!bRotateModeOn);
}

void ACubelithPlayerController::ExitRotateMode()
{
	// 入っていなくても CommitRotateDrag は通す（表示だけのねじれを必ず解く）
	SetRotateMode(false);
	CommitRotateDrag();
}

void ACubelithPlayerController::BeginRotateDrag(int32 PieceId, const FVector2D& ScreenPosition)
{
	const Cubelith::FGame* Game = GetGame();
	if (Game == nullptr || Game->PlacementOf(PieceId) == nullptr)
	{
		UE_LOG(LogCubelith, Warning, TEXT("ピース %d の配置が取れないので回せない"), PieceId);
		return;
	}

	DragMode = ECubelithDragMode::RotatePiece;
	DragPieceId = PieceId;
	DragStartScreenPosition = ScreenPosition;
	// 開始時の姿勢は毎回恒等。ドラッグ量からその都度作り直すので、往復させても誤差が溜まらない
	FreeRotationQuat = FQuat::Identity;
}

void ACubelithPlayerController::UpdateRotateDrag(const FVector2D& ScreenPosition)
{
	if (DragMode != ECubelithDragMode::RotatePiece)
	{
		return;
	}

	FVector WorldRight = FVector::ZeroVector;
	FVector WorldUp = FVector::ZeroVector;
	FVector WorldForward = FVector::ZeroVector;
	if (!GetCameraBasis(WorldRight, WorldUp, WorldForward))
	{
		// カメラが取れないフレームは回さない（次のフレームで開始位置からの総量として取り直される）
		return;
	}

	// 開始位置からの総移動量から作り直す（差分を積み上げない。pieceInput.ts の updateRotateDrag）
	const FQuat Quat = Cubelith::TrackballRotation(
		WorldRight,
		WorldUp,
		ScreenPosition.X - DragStartScreenPosition.X,
		ScreenPosition.Y - DragStartScreenPosition.Y,
		FMath::Max(0.0, RotateDegreesPerPixel));

	FreeRotationQuat = Quat;

	// 見せるだけ。論理上の配置は離すまで変えない（回転の中心はピースの局所原点。RULES.md 3.3）
	if (ACubelithPuzzleActor* PuzzleActor = GetPuzzleActor())
	{
		PuzzleActor->SetFreeRotation(DragPieceId, Quat);
	}
}

void ACubelithPlayerController::CommitRotateDrag()
{
	if (DragMode != ECubelithDragMode::RotatePiece)
	{
		return;
	}

	const int32 PieceId = DragPieceId;
	const FQuat Quat = FreeRotationQuat;

	// 先に状態を畳む。下の Place が OnChange を通して描画を書き直すので、そこから再入しても二重に確定しない
	DragMode = ECubelithDragMode::None;
	DragPieceId = INDEX_NONE;
	FreeRotationQuat = FQuat::Identity;

	// 自由回転の見せ方は先に解く。このあとの Place で 1 回だけ描き直されて、確定した向きがそのまま出る
	if (ACubelithPuzzleActor* PuzzleActor = GetPuzzleActor())
	{
		PuzzleActor->ClearFreeRotation(PieceId);
	}

	// 回したら前のスナップの補間は用済み（main.ts の onRotate / onFreeRotateEnd と同じ）
	CancelSnapMotion(PieceId);

	Cubelith::FGame* Game = GetGame();
	const Cubelith::FPlacement* Placement = (Game != nullptr) ? Game->PlacementOf(PieceId) : nullptr;
	if (Game == nullptr || Placement == nullptr)
	{
		UE_LOG(LogCubelith, Warning, TEXT("ピース %d の配置が取れないので自由回転を確定できなかった"), PieceId);
		return;
	}

	// Place は配置の配列を作り直すので、Placement のポインタが指す先は無効になる。先に写しておく
	const int32 PreviousOrientation = Placement->Orientation;
	const Cubelith::FVec3 Position = Placement->Position;

	// 見せていた姿勢に最も近い 90 度の向きへ（RULES.md 3.3）。位置は据え置き ＝ 局所原点まわりの回転
	const int32 NextOrientation = Cubelith::SnappedOrientation(PreviousOrientation, Quat);
	if (NextOrientation == PreviousOrientation)
	{
		UE_LOG(LogCubelith, Log,
			TEXT("ピース %d の自由回転は向き %d のままに落ちた（90 度に届かなかった）"), PieceId, PreviousOrientation);
		// 向きが変わらなければ OnChange も来ないので、ここで候補を見直す必要も無い
		return;
	}

	Game->Place(PieceId, NextOrientation, Position);
	UE_LOG(LogCubelith, Log, TEXT("ピース %d を回した: 向き %d → %d（回転モードのドラッグを離して確定）"),
		PieceId, PreviousOrientation, NextOrientation);

	// 向きが変わればスナップ候補も変わる（Place の OnChange で既に計算し直されているが、
	// main.ts の onFreeRotateEnd と同じ場所に置いて「回転の確定後に見直す」経路を明示しておく）
	RefreshSnapHint();
}

void ACubelithPlayerController::ApplyTwoFinger(const FVector2D& First, const FVector2D& Second)
{
	const Cubelith::FTwoFingerAction Action = Gesture.Update(First, Second);
	if (Action.Kind == Cubelith::ETwoFingerActionKind::None)
	{
		return;
	}

	if (Action.Kind == Cubelith::ETwoFingerActionKind::Zoom)
	{
		// ズームは固定中のピースを選んでいても効かせる（RULES.md 3.3。選択の有無によらないのが本来）。
		// Pawn 側のタッチのピンチはこの間止めてあるので二重に効かない
		if (ACubelithOrbitPawn* OrbitPawn = GetOrbitPawn())
		{
			OrbitPawn->PinchZoomBy(Action.Scale);
		}
		return;
	}

	// 回転モード中は 2 本指の 90 度回転を行わない（回転はモード中のドラッグへ一本化する。
	// pieceInput.ts の applyTwoFinger と同じ。ズームは上の分岐でどちらでも効く）
	if (bRotateModeOn)
	{
		return;
	}

	// ここから 90 度回転。乗っ取る条件で「回転あり」と「選択あり」は確かめてあるので、残るのは固定と配置
	const int32 PieceId = SelectedPieceId;
	if (PieceId == INDEX_NONE)
	{
		return;
	}

	if (IsPieceLocked(PieceId))
	{
		UE_LOG(LogCubelith, Log,
			TEXT("ピース %d は固定中なので 2 本指の回転を弾いた（ズームは効く。RULES.md 3.3「固定」）"), PieceId);
		return;
	}

	Cubelith::FGame* Game = GetGame();
	if (Game == nullptr)
	{
		return;
	}

	Cubelith::FDragAxes Axes;
	if (!ComputeDragAxes(Axes))
	{
		UE_LOG(LogCubelith, Warning, TEXT("カメラが取れないのでピース %d を 2 本指で回せない"), PieceId);
		return;
	}

	// 画面基準の軸（Yaw = 画面の上、Pitch = 画面の右、Roll = 画面の奥）をグリッド軸へ写し、
	// 「見たまま回る」向きを決める（pieceInput.ts の screenSign）
	const Cubelith::FRotateStep Step = Cubelith::TwoFingerRotateStep(Action, Axes);
	if (Step.Dir == 0)
	{
		return;
	}

	// 回したら前のスナップの補間は用済み（main.ts の onRotate と同じ）
	CancelSnapMotion(PieceId);

	// 局所原点まわりの 90 度（RULES.md 3.3）。1 回のジェスチャで 1 回だけ回る
	// （FTwoFingerGesture が回転を返した時点で Done になり、指を離すまで次を返さない）
	Game->Rotate(PieceId, Step.Axis, Step.Dir);

	UE_LOG(LogCubelith, Log, TEXT("2 本指（%s）でピース %d を %s 軸まわりに %s 90 度回した"),
		TwoFingerGestureName(Action.Gesture), PieceId, LogicAxisName(Step.Axis),
		(Step.Dir > 0) ? TEXT("+") : TEXT("-"));
}

bool ACubelithPlayerController::IsRotationAllowed() const
{
	const ACubelithGameMode* GameMode = GetCubelithGameMode();
	return (GameMode != nullptr) && GameMode->IsRotationAllowed();
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

	// 選択が変わる前に、走っている自由回転を確定させる（回転は選んだピースに紐づく操作。
	// pieceInput.ts の setSelected が commitRotateDrag を通してから選択を差し替えるのと同じ）
	CommitRotateDrag();

	// 選択が変わったら回転モードも抜ける（同じく pieceInput.ts の setSelected）。
	// HUD のラベルは下の RefreshHud で「回転」へ戻る
	bRotateModeOn = false;

	SelectedPieceId = PieceId;

	// 見た目（前の選択を元の色へ戻す・新しい選択を強調する）はピースを描くアクタが受け持つ
	if (ACubelithPuzzleActor* PuzzleActor = GetPuzzleActor())
	{
		PuzzleActor->SetSelectedPiece(PieceId);
	}

	// 選択中はドラッグをピース操作に回す（pieceInput.ts の setSelected が orbit.enabled を切り替えるのと同じ）
	UpdateOrbitEnabled();

	// 候補は選択中のピースについてだけ出す（main.ts の onSelectionChange が snap.refresh を呼ぶのと同じ場所）
	RefreshSnapHint();

	// HUD は選択でボタンを出す / 隠す（RULES.md 6 章）。画面を持っているのは ACubelithGameMode なので
	// そこへ知らせる（main.ts の onSelectionChange が hud.setSelected / setLock を呼ぶのに当たる）
	if (ACubelithGameMode* const GameMode = GetCubelithGameMode())
	{
		GameMode->RefreshHud();
	}

	if (PieceId == INDEX_NONE)
	{
		UE_LOG(LogCubelith, Log, TEXT("ピースの選択を解除した"));
		return;
	}

	// 固定中のピースも選択だけはできる（RULES.md 3.3）。固定を付け外しするのは
	// ACubelithGameMode::ToggleLock / UseHint で、ここは選んだピースの状態をログに出すだけ
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
