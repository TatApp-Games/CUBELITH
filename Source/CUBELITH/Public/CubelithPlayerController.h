// ピースの選択・ドラッグ移動・90 度回転を受け持つ PlayerController（RULES.md 3.3・Docs/SPEC_UE.md 8 章 U3）
// 移植元は WebMock/src/input/pieceInput.ts の pickPiece / setSelected / MoveDrag / applyTwoFinger / RotateDrag。
// 押した瞬間にライントレースでピースを引き、当たったピースを選択中にする。
// 何も無い場所を押しても選択は外さない（RULES.md 3.3。外れるのは「散らし直す」など外からの解除だけで、それは U4）。
//
// ドラッグは押した瞬間に役割が決まり、離すまで変わらない:
//   ピースを押した        → そのピースをカメラの向きに応じた 2 軸へボクセル単位で動かす（軌道カメラの旋回は止める）
//   何も無い場所          → 選択は保ったままカメラを旋回させる（ACubelithOrbitPawn::bOrbitEnabled を戻す）
//   回転モード中にピース  → 選択中のピースを 90 度に縛らず回して見せ、離した時点で最寄りの向きへ確定させる
// ホイールのズームは選択の有無によらず効く（ACubelithOrbitPawn が受け持つ）。
//
// 回転モード（RULES.md 3.3 の「回転モードのドラッグ」）は HUD のトグル（RULES.md 6 章）で出入りする。
// オンの間は選択中のピースへのドラッグが移動ではなく自由回転になり、離すと最寄りの向きで確定する。
// 入れるかどうかを決めるのはこちら（選択が無い / 固定中のピース / パズルの回転「なし」では入らない）で、
// その結果を HUD のラベルへ返す（Web 版 main.ts の onToggleRotateMode と同じ形）。
// マウスでも指でも同じように働く（U3 にあったマウスの右ボタン専用の自由回転は、この回転モードに置き換えた）。
//
// 2 本指（タッチ）は、パズルの回転「あり」でピースを選んでいる間だけこのコントローラが乗っ取り、
// Cubelith::FTwoFingerGesture に通して「90 度回転」か「ピンチのズーム」に振り分ける。
// 乗っ取っている間は ACubelithOrbitPawn::bTouchPinchEnabled を false にして、同じピンチが二重に効かないようにする。
// 回転「なし」の盤面と未選択のときは乗っ取らないので、2 本指はそのまま Pawn のピンチズームになる（RULES.md 3.1）。
//
// 指 / ボタンを離した時点でマグネット・スナップを掛ける（RULES.md 3.5。移植元は
// WebMock/src/input/snapControl.ts を移した Cubelith::FSnapControl と、見た目を追いつかせる
// Cubelith::FSnapMotion）。候補がある間の薄い発光は ACubelithPuzzleActor::SetSnapHint（RULES.md 5.1）、
// 吸着した瞬間の効果音は ACubelithGameMode::PlaySnapSound へ回す。
//
// クリアすると ACubelithGameMode がピースの操作だけを止める（SetPieceInputEnabled）。カメラの旋回と
// ズームは残す（RULES.md 5.2-4 の扱いはその宣言の「解釈:」）。
//
// 解釈: RULES.md 3.3 は回転の手段として「2 本指スワイプ / ひねり、回転モードのドラッグ、または
// 回転ギズモ」を挙げているが、**回転ギズモは作らない**（要望が置き換えを求めているのは
// 「マウスで回転を確かめる仮の手段 → HUD の回転モードのトグル」だけ）。足すかは後の段階で人が決める。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "CubelithAxisMapping.h"
#include "CubelithPickSamples.h"
#include "CubelithRotateInput.h"
#include "CubelithSnapControl.h"
#include "CubelithSnapMotion.h"
#include "CubelithTwoFingerGesture.h"

#include "CubelithPlayerController.generated.h"

class ACubelithGameMode;
class ACubelithOrbitPawn;
class ACubelithPuzzleActor;

namespace Cubelith
{
	class FGame;
}

/**
 * ドラッグ 1 本分の役割。押した瞬間に決めて、指 / ボタンを離すまで変えない（RULES.md 3.3）。
 * UENUM にはしない（Blueprint へ出す必要が無く、.uasset も作らないため）。
 */
enum class ECubelithDragMode : uint8
{
	/** ドラッグしていない */
	None,

	/** 押したピースをボクセル単位で動かしている */
	MovePiece,

	/** 何も無い場所から始めたドラッグ。カメラの旋回に回す（選択は保つ） */
	OrbitCamera,

	/**
	 * 選択中のピースを自由回転させている（回転モード中のドラッグ。90 度に縛らず見せるだけで、
	 * 離した時点で最寄りの向きへ確定させる。pieceInput.ts の RotateDrag に当たる）
	 */
	RotatePiece,
};

/**
 * ピースの選択とドラッグ移動を受け持つ PlayerController。ACubelithGameMode がコンストラクタで
 * PlayerControllerClass に指定する。
 *
 * 入力は Enhanced Input の InputAction / InputMappingContext（= `.uasset`）を使わず、PlayerTick で
 * APlayerController から生の入力状態をポーリングして読む（Docs/SPEC_UE.md 0 章・4 章。
 * ACubelithOrbitPawn::PollInput と同じやり方）。ポーリングなので「押した瞬間」のイベントが無く、
 * 前フレームの押下状態と比べて立ち上がりを自分で見る。
 */
UCLASS()
class CUBELITH_API ACubelithPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACubelithPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	/** 選択中のピース id。未選択は INDEX_NONE */
	int32 GetSelectedPieceId() const { return SelectedPieceId; }

	/**
	 * 選択を置き換える（INDEX_NONE で解除）。見た目への反映は ACubelithPuzzleActor::SetSelectedPiece に任せる。
	 * 外からの解除（「散らし直す」など。RULES.md 3.3）は U4 でここを呼ぶ
	 */
	void SetSelectedPiece(int32 PieceId);

	/**
	 * 盤面が入れ替わるときに操作の状態を白紙へ戻す（ACubelithGameMode::EndSession が呼ぶ）。
	 *
	 * 選択・ドラッグ・2 本指・自由回転・スナップはどれも前の盤面のピースに紐づくので、
	 * セッションを作り直すたびに捨てる。**ピース数が同じでもシードが違えば形が変わる**ので、
	 * スナップの制御は EnsureSnapControl の「ピース数が変わったら作り直す」判定に頼らず必ず作り直させる
	 */
	void ResetForNewSession();

	/**
	 * 走っている自由回転を最寄りの向きへ確定させる（掛かっていなければ何もしない）。
	 *
	 * 固定・ヒント・散らし直し（RULES.md 3.3 / 3.7）は、その前に表示上のねじれを片付けておく必要がある
	 * （固定すると回せなくなり、ヒントと散らし直しはピースを別の場所へ動かすため）。
	 * ACubelithGameMode がそれらの操作の入口で呼ぶ（main.ts の exitRotateMode に当たる）
	 */
	void CommitFreeRotation();

	/** 回転モード（RULES.md 3.3 の「回転モードのドラッグ」）が入っているか。HUD のラベルに使う */
	bool IsRotateModeOn() const { return bRotateModeOn; }

	/**
	 * 回転モードを入れる / 抜ける（HUD のトグルから呼ぶ。pieceInput.ts の setRotateMode）。
	 *
	 * **入れるかどうかはここで決める**: 選択が無い・固定中のピース・パズルの回転「なし」では入らない。
	 * 抜けるときは走っている自由回転を確定させて表示だけのねじれを解く（U3 の CommitRotateDrag を通す）。
	 * 戻り値は結果の状態（呼び出し側がそのまま HUD のラベルへ返す。main.ts の onToggleRotateMode と同じ形）
	 */
	bool SetRotateMode(bool bEnabled);

	/** 回転モードを切り替える（HUD の「回転 / 回転解除」。戻り値は結果の状態） */
	bool ToggleRotateMode();

	/**
	 * 回転モードを抜ける（入っていなければ表示のねじれを解くだけ）。
	 * 固定・ヒント・散らし直し・クリアのときに ACubelithGameMode が呼ぶ（main.ts の exitRotateMode）
	 */
	void ExitRotateMode();

	/**
	 * ピースの操作を受け付けるかを切り替える（クリアしたら止める。ACubelithGameMode::ShowClear が呼ぶ）。
	 *
	 * 止めると、ピースの選択・ドラッグ移動・回転モードのドラッグ・2 本指の 90 度回転・
	 * 離したときのスナップがどれも効かなくなる。**カメラの旋回とズームは残す**
	 * （解釈: RULES.md 5.2-4 の「操作は無効化」とカメラの自動旋回はクリア演出（U5）の一部なので、
	 * この段階ではピースの操作だけを止める）。
	 *
	 * 止めるときは、走っている回転モードのねじれ（自由回転）を最寄りの向きで確定させ、スナップの補間を
	 * 打ち切って表示上のずれを戻し、ピースの選択も解く（クリアした形が歪んだまま・ずれたまま残らないように）。
	 * 受け付ける側へ戻すのは新しい盤面を作るとき（ResetForNewSession）なので、呼ぶ側は止める側だけを使う
	 */
	void SetPieceInputEnabled(bool bEnabled);

	/** ピースの操作を受け付けるか（クリア画面を出している間だけ false） */
	bool IsPieceInputEnabled() const { return bPieceInputEnabled; }

	/**
	 * そのピースに走っているスナップの補間を打ち切る（RULES.md 3.5 の見た目の追いつき）。
	 * 固定した位置・ヒントで送った位置でそのまま止めるために、ACubelithGameMode が呼ぶ
	 */
	void CancelSnapMotionFor(int32 PieceId);

	/**
	 * 走っているスナップの補間をすべて打ち切り、表示だけのずれを戻す（散らし直し。RULES.md 3.3「やり直し」）。
	 * ResetForNewSession と違い、戻す相手（ピースのアクタ）はそのまま残るのでずれの解除まで流す
	 */
	void CancelAllSnapMotion();

	/**
	 * スナップ候補（＝薄く光らせる対象。RULES.md 5.1）を計算し直す（snapControl.ts の refresh）。
	 * 配置が変わったとき・選択が変わったとき・回転が確定したときに呼ぶ。毎フレームは回さない。
	 * 固定中のピースは候補を出さない（TS の main.ts が lockKindOf を見て null を渡すのと同じ）。
	 *
	 * 固定を付け外しすると候補の有無が変わるが、固定は配置を変えないので Cubelith::FGame の
	 * OnChange が来ない ＝ ACubelithGameMode が固定の操作のあとに明示的に呼ぶ
	 */
	void RefreshSnapHint();

	/**
	 * タッチの近接ピックの許容半径（CSS ピクセル相当。pieceInput.ts の TOUCH_PICK_RADIUS_PX）。
	 * 指の接触面は広く狙いも粗いのでマウスより広く取る
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "0.0"))
	double TouchPickRadiusPx = 16.0;

	/** マウスの近接ピックの許容半径（CSS ピクセル相当。pieceInput.ts の PRECISE_PICK_RADIUS_PX） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "0.0"))
	double MousePickRadiusPx = 8.0;

	/**
	 * 近接ピックの半径を画面の DPI スケールで割り増すか。
	 *
	 * 解釈: 上の半径はブラウザの CSS ピクセル（= 端末非依存のピクセル）で決めた値だが、
	 * DeprojectScreenPositionToWorld とタッチ座標が使うのはビューポートのピクセル（GetViewportSize の単位。
	 * 高 DPI の端末では 1 CSS ピクセルが複数ピクセルに当たる）。そこで UMG と同じ DPI スケール
	 * （UUserInterfaceSettings::GetDPIScaleBasedOnSize）を掛けて、指の太さに対する当たりの広さを端末間で揃える。
	 * 厳密な一致は求めない（手触りの調整は人が半径そのものを動かして行う）
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input")
	bool bScalePickRadiusByDpi = true;

	/** ピック用のライントレースを飛ばす長さ（cm）。カメラの遠クリップより十分長ければよい */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "1.0"))
	double PickTraceDistanceCm = 1000000.0;

	/**
	 * 1 マス動かすのに必要なドラッグ量（px）の下限（pieceInput.ts の MIN_PIXELS_PER_VOXEL）。
	 * 基準は「ピースの奥行きでの 1 ボクセルの画面上の大きさ」なので、画面サイズやカメラ距離が変わっても
	 * 「ボクセル 1 個分ドラッグすれば 1 マス動く」体感になる。極端なズームで感度が壊れないよう両端で丸める
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "1.0"))
	double MinPixelsPerVoxel = 14.0;

	/** 同じくドラッグ量（px）の上限（pieceInput.ts の MAX_PIXELS_PER_VOXEL） */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "1.0"))
	double MaxPixelsPerVoxel = 160.0;

	/**
	 * 回転モード中のドラッグでピースを回す感度（ドラッグ 1 px あたりの回転角・度）。
	 * 既定は pieceInput.ts の ROTATE_DEGREES_PER_PIXEL（90 度回すのに 225 px）
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "0.0"))
	double RotateDegreesPerPixel = Cubelith::RotateDegreesPerPixel;

	/**
	 * スナップの見た目が論理位置に追いつくまでの時間（秒。RULES.md 3.5 の 100〜150 ms）。
	 * 既定は snapMotion.ts の SNAP_DURATION_MS = 130 ms と同じ。0 なら補間せず即座に置く。
	 * 論理上の配置は常に離した瞬間に確定するので、この値は手触りにしか効かない
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Input", meta = (ClampMin = "0.0"))
	double SnapDurationSeconds = Cubelith::SnapDurationSeconds;

protected:
	virtual void BeginPlay() override;

private:
	/** マウスとタッチの押下・移動・解放をポーリングで拾い、押下の立ち上がり / 立ち下がりを自分で見る */
	void PollPointer();

	/** 押した瞬間の処理。ピースに当たれば選択してドラッグ移動を始め、外れればカメラの旋回に回す */
	void HandlePointerPressed(const FVector2D& ScreenPosition, bool bTouch);

	/**
	 * 押したまま動かしたときの処理。ドラッグ移動の最中だけ、開始位置からの画面上の移動量を
	 * マス数に直して Cubelith::FGame::Move へ流す（pieceInput.ts の onPointerMove の移動の部分）
	 */
	void HandlePointerMoved(const FVector2D& ScreenPosition);

	/**
	 * 離したときの処理。ドラッグの役割を畳んでから、ピースを動かしていたならマグネット・スナップを掛ける
	 * （RULES.md 3.5。pieceInput.ts の onPointerUp が onRelease を呼ぶのと同じ場所）
	 */
	void HandlePointerReleased();

	/**
	 * 離した時点で吸着させる（snapControl.ts の release + main.ts の onSnap）。
	 * 吸着先があれば Cubelith::FGame::Move で論理上の配置を確定させ、見た目だけを
	 * Cubelith::FSnapMotion で追いつかせ、効果音を鳴らす。固定中のピースは吸着させない（RULES.md 3.3）
	 */
	void ApplySnapOnRelease(int32 PieceId);

	/** 配置が変わったときに ACubelithGameMode から呼ばれる（OnPlacementsChanged に乗せる口） */
	void HandlePlacementsChanged(TArrayView<const Cubelith::FPlacement> Placements);

	/** 走っているスナップの補間を打ち切る（手で動かした / 回した / 固定したとき。TS の snapMotion.cancel） */
	void CancelSnapMotion(int32 PieceId);

	/** 補間を 1 フレーム進める（PlayerTick から呼ぶ。TS の session.update の snapMotion.update） */
	void UpdateSnapMotion();

	/** Cubelith::FSnapMotion が出した表示上のずれを ACubelithPuzzleActor へ流す */
	void ApplySnapOffsets();

	/**
	 * スナップの制御をこの盤面のピースで用意する（まだなら）。パズルが開くのは GameMode の BeginPlay で、
	 * この PlayerController の BeginPlay との順序は決まっていないので、使う直前に作る
	 */
	void EnsureSnapControl();

	/**
	 * 回転モード中に選択中のピースを押したときのドラッグを始める（pieceInput.ts の rotate のドラッグ）。
	 * 呼ぶ前に「回転モードが入っている・そのピースが選択中で固定されていない」ことは済んでいる
	 * （回転モードに入る条件がそれを含む）。配置が取れなければ始めない
	 */
	void BeginRotateDrag(int32 PieceId, const FVector2D& ScreenPosition);

	/**
	 * 回転モード中に押したまま動かしたときの処理。開始位置からの移動量で作り直した回転を
	 * ACubelithPuzzleActor::SetFreeRotation に流して見せるだけで、論理上の配置は変えない
	 */
	void UpdateRotateDrag(const FVector2D& ScreenPosition);

	/**
	 * 走っている自由回転を最寄りの向きへ確定させる（pieceInput.ts の commitRotateDrag）。
	 * Cubelith::SnappedOrientation で向き id を決めて Cubelith::FGame::Place で反映し、
	 * 自由回転の見せ方を解く。指を離す以外の理由で終わるとき（選択が変わる・指が 2 本になる）も必ずここを通す
	 * ＝ 表示だけねじれたピースが残らない
	 */
	void CommitRotateDrag();

	/**
	 * 2 本指ジェスチャを 1 回分処理する（pieceInput.ts の applyTwoFinger）。
	 * 回転なら Cubelith::FGame::Rotate、ズームなら ACubelithOrbitPawn::PinchZoomBy に流す
	 */
	void ApplyTwoFinger(const FVector2D& First, const FVector2D& Second);

	/**
	 * この盤面でパズルの回転が「あり」か（RULES.md 3.1）。ACubelithGameMode が決めた値を読む。
	 * 「なし」なら 2 本指の 90 度回転も回転モードも使えない（ピンチのズームだけが残る）
	 */
	bool IsRotationAllowed() const;

	/**
	 * ドラッグ移動を始める。開始画面座標・ドラッグ軸・感度をこの時点で固定するので、
	 * 途中でカメラが動いても写像や 1 マスの長さがぶれない（pieceInput.ts の MoveDrag）
	 */
	void BeginMoveDrag(int32 PieceId, const FVector2D& ScreenPosition);

	/** ドラッグを畳み、軌道カメラの旋回を「選択中は止める」状態へ戻す（選択は保つ） */
	void EndDrag();

	/** プレイヤーのカメラの基底（UE のワールド座標）。取れなければ false */
	bool GetCameraBasis(FVector& OutRight, FVector& OutUp, FVector& OutForward) const;

	/** 今のカメラからドラッグ軸の割り当てを作る（pieceInput.ts の currentAxes）。取れなければ false */
	bool ComputeDragAxes(Cubelith::FDragAxes& OutAxes) const;

	/**
	 * ピースの奥行きでの「1 ボクセル = 何 px」（pieceInput.ts の pixelsPerVoxelAt）。
	 * MinPixelsPerVoxel 〜 MaxPixelsPerVoxel で丸めた値を返す
	 */
	double PixelsPerVoxelAt(int32 PieceId) const;

	/** 固定中（RULES.md 3.3「固定」）のピースか。ゲーム状態が無ければ false */
	bool IsPieceLocked(int32 PieceId) const;

	/** 軌道カメラの旋回の有効・無効を、今の選択とドラッグの役割から決め直す */
	void UpdateOrbitEnabled();

	/** ゲーム状態。まだ作られていなければ nullptr */
	Cubelith::FGame* GetGame() const;

	/** 操作しているプレイヤーの軌道カメラ。軌道カメラでなければ nullptr */
	ACubelithOrbitPawn* GetOrbitPawn() const;

	/**
	 * 画面座標のピースを拾う。何も無ければ INDEX_NONE（pieceInput.ts の pickPiece）。
	 * 中心のレイが当たればそれを採り、外れたときだけ近接サンプルを撃ってレイ原点に最も近いヒットを選ぶ
	 */
	int32 PickPieceAtScreenPosition(const FVector2D& ScreenPosition, bool bTouch);

	/**
	 * レイ 1 本を撃って最も手前のピースを返す。当たらなければ INDEX_NONE。
	 * ピース以外の物（床など）に遮られても、その後ろのピースを拾えるよう複数ヒットで撃つ
	 * （pieceInput.ts のレイキャストがピースだけを対象にしているのに合わせる）
	 */
	int32 TraceNearestPiece(const FVector& WorldOrigin, const FVector& WorldDirection, double& OutDistance) const;

	/** 近接ピックのサンプル点を（必要なら作り直して）用意する */
	void EnsurePickOffsets();

	/** CSS ピクセル → ビューポートのピクセルの倍率（bScalePickRadiusByDpi が false なら 1） */
	double GetPickRadiusScale() const;

	ACubelithGameMode* GetCubelithGameMode() const;
	ACubelithPuzzleActor* GetPuzzleActor() const;

	/**
	 * 近接ピックのサンプル点（Cubelith::PickSampleOffsets）。半径ごとに固定なので BeginPlay で 1 度作って
	 * 使い回す。DPI スケールが変わったとき（ウィンドウを別の画面へ動かした等）だけ作り直す
	 */
	TArray<Cubelith::FPickOffset> TouchPickOffsets;
	TArray<Cubelith::FPickOffset> MousePickOffsets;

	/** 上のサンプル点を作ったときの倍率。負なら未作成 */
	double PickOffsetsScale = -1.0;

	/** 前フレームの押下状態（ポーリングなので立ち上がりを自分で見る） */
	bool bWasTouchDown = false;
	bool bWasMouseDown = false;

	/**
	 * 回転モードが入っているか（pieceInput.ts の rotateModeOn）。オンの間は選択中のピースへの
	 * ドラッグが自由回転になる。固定したピースを選んでいる間はフラグが立っていても効かない
	 * （ドラッグを始めるときに固定を見る。hud.ts の render もそのときラベルを「回転」へ戻す）
	 */
	bool bRotateModeOn = false;

	/**
	 * ピースの操作を受け付けるか（SetPieceInputEnabled）。クリアしている間だけ false になる。
	 * ResetForNewSession が true へ戻す ＝ 盤面を作り直せば必ず操作できる
	 */
	bool bPieceInputEnabled = true;

	/** 2 本指ジェスチャの状態機械（ピンチか 90 度回転かを判定する。移植元 twoFingerGesture.ts） */
	Cubelith::FTwoFingerGesture Gesture;

	/**
	 * 選択中の 2 本指をこのコントローラが乗っ取っているか（pieceInput.ts の gestureActive）。
	 * 追う 2 本は ETouchIndex::Touch1 / Touch2 の組で、UE のタッチ index は指ごとにその指の寿命の間
	 * 変わらないので、この 2 つがそのまま「先に触れた 2 本を触れた順で固定したもの」になる
	 * （ひねりの向きは組の順で反転するので、順序の安定が要る）。どちらかが離れたらジェスチャを終える
	 */
	bool bTwoFingerActive = false;

	/** 選択中のピース id（未選択は INDEX_NONE） */
	int32 SelectedPieceId = INDEX_NONE;

	/** 今のドラッグの役割。押した瞬間に決まり、離すまで変わらない */
	ECubelithDragMode DragMode = ECubelithDragMode::None;

	/** ドラッグ移動しているピース id（していなければ INDEX_NONE） */
	int32 DragPieceId = INDEX_NONE;

	/** ドラッグを始めた画面座標（Y は下向きが正）。移動量はここからの差で測る */
	FVector2D DragStartScreenPosition = FVector2D::ZeroVector;

	/** 開始時に固定したドラッグ軸（画面の右 / 上がどのグリッド軸に当たるか） */
	Cubelith::FDragAxes ActiveDragAxes;

	/** 開始時に固定した感度（1 ボクセル = 何 px） */
	double DragPixelsPerVoxel = 1.0;

	/** これまでに Cubelith::FGame::Move へ流したマス数。差分だけを流すために持つ */
	int32 DragAppliedRight = 0;
	int32 DragAppliedUp = 0;

	/**
	 * 自由回転で今見せている回転（UE ワールド。DragMode == RotatePiece の間だけ意味を持つ）。
	 * 開始時の姿勢（pieceInput.ts の base）は毎回恒等で、毎フレーム「開始位置からの総移動量」から
	 * 作り直す（差分を積み上げて誤差を溜めない）
	 */
	FQuat FreeRotationQuat = FQuat::Identity;

	/**
	 * スナップの制御（snapControl.ts）。構築時にピースと N が要るので、パズルが開いたあと
	 * EnsureSnapControl で作り直す。どの盤面で作ったかは下の SnapControlPieceCount で見る
	 */
	Cubelith::FSnapControl SnapControl;

	/**
	 * SnapControl を作ったときのピース数。0 なら未作成。パズルを作り直すと数が変わるので、
	 * 食い違いを見て作り直す（U4 の「次の問題」で盤面が入れ替わる道に備える）
	 */
	int32 SnapControlPieceCount = 0;

	/** スナップの補間移動（snapMotion.ts）。時間は UWorld の時刻を渡す */
	Cubelith::FSnapMotion SnapMotion;

	/**
	 * Cubelith::FSnapMotion が出したずれの受け皿。毎フレーム使い回して確保を避ける
	 * （走っている補間が無ければ空のまま）
	 */
	TArray<Cubelith::FSnapOffsetUpdate> SnapOffsetBuffer;
};
