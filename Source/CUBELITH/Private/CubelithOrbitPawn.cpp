// 軌道カメラの Pawn の実装。移植元は WebMock/src/render/camera.ts

#include "CubelithOrbitPawn.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

ACubelithOrbitPawn::ACubelithOrbitPawn()
{
	// 入力は Tick でポーリングして読む（入力アセットを作らないため。Docs/SPEC_UE.md 0 章）
	PrimaryActorTick.bCanEverTick = true;

	// コントローラの回転をカメラに混ぜない（向きは球面座標の現在値だけで決める）
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	OrbitRoot = CreateDefaultSubobject<USceneComponent>(TEXT("OrbitRoot"));
	SetRootComponent(OrbitRoot);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(OrbitRoot);
	SpringArm->TargetArmLength = static_cast<float>(InitialDistanceCm);
	// 当たり判定で寄られると距離が跳ねる。減衰は自前で持つのでアームのラグも使わない
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = false;
	SpringArm->bEnableCameraRotationLag = false;
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritYaw = false;
	SpringArm->bInheritRoll = false;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	CameraComponent->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	CameraComponent->bUsePawnControlRotation = false;
	CameraComponent->SetFieldOfView(static_cast<float>(FieldOfViewDeg));
}

void ACubelithOrbitPawn::BeginPlay()
{
	Super::BeginPlay();

	ResetToInitial();
}

void ACubelithOrbitPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	PollInput();
	AdvanceTowardTarget(DeltaSeconds);
	ApplyToComponents();
}

void ACubelithOrbitPawn::SetOrbitTarget(const FVector& WorldTarget)
{
	// 注視点はルート（= このアクタの位置）。スプリングアームがそこからの距離と向きを受け持つ
	SetActorLocation(WorldTarget);
}

void ACubelithOrbitPawn::FrameSphere(double BoundingRadiusCm)
{
	if (!(BoundingRadiusCm > 0.0))
	{
		return;
	}

	const double SinHalfFov = FMath::Sin(GetFitHalfFovRadians());
	if (SinHalfFov <= UE_DOUBLE_SMALL_NUMBER)
	{
		return;
	}

	// camera.ts の frame と同じ。球が画角に収まる距離 r / sin(halfFov) に余裕を掛け、下限・上限で丸める
	SetTargetDistance(BoundingRadiusCm / SinHalfFov * FMath::Max(1.0, FrameMarginScale));
	// camera.ts の frame も radius を goalRadius に揃えている（引き直しを待たせずに見せる）
	CurrentDistanceCm = TargetDistanceCm;
	ApplyToComponents();
}

void ACubelithOrbitPawn::ResetToInitial()
{
	const double ElevationLimit = FMath::Abs(MaxElevationDeg);
	TargetAzimuthDeg = FMath::UnwindDegrees(InitialAzimuthDeg);
	TargetElevationDeg = FMath::Clamp(InitialElevationDeg, -ElevationLimit, ElevationLimit);
	SetTargetDistance(InitialDistanceCm);

	CurrentAzimuthDeg = TargetAzimuthDeg;
	CurrentElevationDeg = TargetElevationDeg;
	CurrentDistanceCm = TargetDistanceCm;

	bWasTouch1Down = false;
	bWasTouch2Down = false;
	PreviousPinchDistance = 0.0;

	if (CameraComponent != nullptr)
	{
		CameraComponent->SetFieldOfView(static_cast<float>(FieldOfViewDeg));
	}
	ApplyToComponents();
}

void ACubelithOrbitPawn::PollInput()
{
	// Enhanced Input の InputAction / InputMappingContext は .uasset なので使わず、UPlayerInput が持つ
	// 生のキー状態を毎フレーム読む（アセットを作らないため。Docs/SPEC_UE.md 0 章）。
	// 入力マッピング（Config/DefaultInput.ini）にも依存しない
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (PlayerController == nullptr)
	{
		return;
	}

	double Touch1X = 0.0;
	double Touch1Y = 0.0;
	double Touch2X = 0.0;
	double Touch2Y = 0.0;
	bool bTouch1Down = false;
	bool bTouch2Down = false;
	PlayerController->GetInputTouchState(ETouchIndex::Touch1, Touch1X, Touch1Y, bTouch1Down);
	PlayerController->GetInputTouchState(ETouchIndex::Touch2, Touch2X, Touch2Y, bTouch2Down);

	// bOrbitEnabled が false でも指の位置は追い続ける（止まるのは旋回だけで、ピンチのズームは効かせるため。
	// RULES.md 3.3「ホイール / ピンチでズーム（ピースの選択の有無によらない）」）。
	// 旋回の有無はドラッグを始めた時点で決まり、途中で変わらない（ACubelithPlayerController が決める）ので、
	// 「無効の間の指の動きが有効に戻った瞬間に効いてカメラが飛ぶ」ことは起きない

	if (bTouch1Down && bTouch2Down)
	{
		// 2 本指: 2 点間の距離の比で距離を変える（camera.ts の 2 本指ピンチ）。指を広げると寄る。
		// ズームなので bOrbitEnabled は見ない。ただし選択中の 2 本指をコントローラが乗っ取っている間は
		// bTouchPinchEnabled が false になり、同じピンチがあちらのジェスチャからも掛かる二重掛けを避ける
		const double PinchDistance = FVector2D::Distance(FVector2D(Touch1X, Touch1Y), FVector2D(Touch2X, Touch2Y));
		if (bTouchPinchEnabled && bWasTouch1Down && bWasTouch2Down && PreviousPinchDistance > 0.0 && PinchDistance > 0.0)
		{
			PinchZoomBy(PreviousPinchDistance / PinchDistance);
		}
		// 止めている間も指の間隔は追い続ける（有効に戻った瞬間に距離が飛ばないように。旋回と同じ考え方）
		PreviousPinchDistance = PinchDistance;
	}
	else if (bTouch1Down)
	{
		// 1 本指: 旋回。タッチの座標は画面の座標（Y は下向きが正）なので camera.ts の dy としてそのまま使える。
		// 直前が 2 本指だったフレームは差分を使わない（指を 1 本離した瞬間にカメラが飛ぶため）
		if (bOrbitEnabled && bWasTouch1Down && !bWasTouch2Down)
		{
			AddOrbitDelta(Touch1X - PreviousTouch1.X, Touch1Y - PreviousTouch1.Y);
		}
		PreviousPinchDistance = 0.0;
	}
	else
	{
		PreviousPinchDistance = 0.0;
	}

	PreviousTouch1 = FVector2D(Touch1X, Touch1Y);
	bWasTouch1Down = bTouch1Down;
	bWasTouch2Down = bTouch2Down;

	if (bTouch1Down || bTouch2Down)
	{
		// タッチが左クリックとしても流れてくる環境（Use Mouse for Touch）で二重に回さない
		return;
	}

	if (bOrbitEnabled && PlayerController->IsInputKeyDown(EKeys::LeftMouseButton))
	{
		double MouseDeltaX = 0.0;
		double MouseDeltaY = 0.0;
		PlayerController->GetInputMouseDelta(MouseDeltaX, MouseDeltaY);
		// UE のマウスの Y 軸は上が正（画面の座標と逆向き）なので、camera.ts の dy に合わせて符号を反転する
		AddOrbitDelta(MouseDeltaX, -MouseDeltaY);
	}

	// そのフレームのホイールの回転量（1 ノッチで ±1。手前に転がすと正）。
	// ホイールのズームは旋回を止めている間（ピースを選択中）も効かせる（RULES.md 3.3）
	const double WheelNotches = PlayerController->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
	if (!FMath::IsNearlyZero(WheelNotches) && WheelZoomScalePerNotch > 1.0)
	{
		// camera.ts はブラウザの deltaY（下へ転がすと正 = 引く）で exp(deltaY * 0.0015)。UE のホイール軸は
		// 手前に転がすと正なので符号が逆になる。手前に転がして寄るのが camera.ts と同じ手触り
		MultiplyTargetDistance(FMath::Pow(WheelZoomScalePerNotch, -WheelNotches));
	}
}

void ACubelithOrbitPawn::PinchZoomBy(double Scale)
{
	if (!FMath::IsFinite(Scale) || Scale <= 0.0)
	{
		return;
	}

	// 解釈: camera.ts は間隔の比をそのまま掛ける。感度はその比の指数として掛ける（1 で camera.ts と同じ）
	MultiplyTargetDistance(FMath::Pow(Scale, FMath::Max(0.0, PinchZoomSensitivity)));
}

void ACubelithOrbitPawn::AddOrbitDelta(double ScreenDeltaX, double ScreenDeltaY)
{
	// camera.ts と同じ向き: 右へ動かすと注視点のまわりを水平に回り（UE のヨーは camera.ts の azimuth と
	// 逆回りなので、符号を反転せずに足すとこちらが camera.ts に一致する）、下へ動かすとカメラが上に回り込む
	TargetAzimuthDeg = FMath::UnwindDegrees(TargetAzimuthDeg + ScreenDeltaX * RotateSensitivityDegPerPixel);

	const double ElevationLimit = FMath::Abs(MaxElevationDeg);
	TargetElevationDeg = FMath::Clamp(
		TargetElevationDeg + ScreenDeltaY * RotateSensitivityDegPerPixel,
		-ElevationLimit,
		ElevationLimit);
}

void ACubelithOrbitPawn::MultiplyTargetDistance(double Scale)
{
	if (!FMath::IsFinite(Scale) || Scale <= 0.0)
	{
		return;
	}
	SetTargetDistance(TargetDistanceCm * Scale);
}

void ACubelithOrbitPawn::SetTargetDistance(double NewDistanceCm)
{
	const double Lower = FMath::Max(1.0, MinDistanceCm);
	const double Upper = FMath::Max(Lower, MaxDistanceCm);
	TargetDistanceCm = FMath::Clamp(NewDistanceCm, Lower, Upper);
}

void ACubelithOrbitPawn::AdvanceTowardTarget(double DeltaSeconds)
{
	// camera.ts の DAMPING = 0.18 は 60 fps の 1 フレームあたりの割合。フレームレートに依存しないよう
	// 時間から係数を作る（1 - exp(-Rate * dt) が dt = 1/60 で 0.18 になる Rate が DampingRate の既定値）
	const double Alpha = (DampingRate > 0.0 && DeltaSeconds > 0.0)
		? 1.0 - FMath::Exp(-DampingRate * DeltaSeconds)
		: 1.0;

	// 方位角は最短の向きに回す（-180..180 度で持ち、際限なく増えないようにする）
	CurrentAzimuthDeg = FMath::UnwindDegrees(CurrentAzimuthDeg + FMath::UnwindDegrees(TargetAzimuthDeg - CurrentAzimuthDeg) * Alpha);
	CurrentElevationDeg += (TargetElevationDeg - CurrentElevationDeg) * Alpha;
	CurrentDistanceCm += (TargetDistanceCm - CurrentDistanceCm) * Alpha;
}

void ACubelithOrbitPawn::ApplyToComponents()
{
	if (SpringArm == nullptr)
	{
		return;
	}

	// スプリングアームはカメラを「向きの逆方向へ TargetArmLength だけ」置く。仰角が正（カメラが注視点より
	// 上）のときピッチは負（見下ろし）になるので、符号を反転して渡す
	SpringArm->SetRelativeRotation(FRotator(-CurrentElevationDeg, CurrentAzimuthDeg, 0.0));
	SpringArm->TargetArmLength = static_cast<float>(CurrentDistanceCm);
}

double ACubelithOrbitPawn::GetFitHalfFovRadians() const
{
	const double HalfFovRad = FMath::DegreesToRadians(FMath::Clamp(FieldOfViewDeg, 1.0, 170.0) * 0.5);

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->GetViewportSize(ViewportX, ViewportY);
	}
	if (ViewportX <= 0 || ViewportY <= 0)
	{
		return HalfFovRad;
	}

	// 解釈: camera.ts は垂直画角だけで距離を決めているが、これは横長のブラウザ窓では垂直が狭い側だから。
	// UE の既定（AspectRatioAxisConstraint = MaintainYFOV）では視野角が画面の長辺に効き、短辺の画角は
	// 縦横比の分だけ狭くなる。縦持ち（Docs/SPEC_UE.md 0 章）では狭い側が水平になるので、
	// 「球が画面に収まる」を満たすよう狭い側の画角で距離を求める。横長の画面では狭い側が垂直になり、
	// camera.ts と同じ式に一致する
	const double ShortPerLongAxis =
		static_cast<double>(FMath::Min(ViewportX, ViewportY)) / static_cast<double>(FMath::Max(ViewportX, ViewportY));
	return FMath::Atan(FMath::Tan(HalfFovRad) * ShortPerLongAxis);
}
