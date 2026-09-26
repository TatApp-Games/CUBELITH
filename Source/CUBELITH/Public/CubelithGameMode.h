// ゲームの既定の GameMode。既定の Pawn を軌道カメラ（ACubelithOrbitPawn）にし、
// 起動するとタイトル / 難易度選択画面を出し、「開始」でパズル 1 回分（= セッション）を作る（Docs/SPEC_UE.md 8 章 U4）
// Config/DefaultEngine.ini の GlobalDefaultGameMode がこのクラスを指している

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/GameModeBase.h"
#include "Templates/SubclassOf.h"
#include "Templates/UniquePtr.h"

#include "CubelithDifficulty.h"
#include "Game.h"
#include "Generate.h"

#include "CubelithGameMode.generated.h"

class ACubelithPlayerController;
class ACubelithPuzzleActor;
class UCubelithScreenWidget;
class USoundBase;
struct FCubelithTitleSelection;

/**
 * 配置が変わったことを操作側へ伝える口（Cubelith::FGame の OnChange から中継する）。
 * 引数は変わった後の全ピースの配置。Cubelith::FPlacement は USTRUCT ではないので
 * 動的デリゲート（DECLARE_DYNAMIC_*）にはできない ＝ 素のマルチキャストデリゲートにする。
 * ACubelithPlayerController がここに乗ってスナップ候補の発光を計算し直す（RULES.md 5.1）
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCubelithPlacementsChanged, TArrayView<const Cubelith::FPlacement>);

/**
 * 今どの画面を出しているか（RULES.md 2 章のコアゲームループ。Web 版 WebMock/src/ui/screens.ts の ScreenName）。
 * UENUM にはしない（Blueprint へ出す必要が無く、`.uasset` も作らないため。ECubelithDragMode と同じ扱い）
 */
enum class ECubelithScreen : uint8
{
	/** まだ何も出していない */
	None,
	/** タイトル / 難易度選択（RULES.md 6 章） */
	Title,
	/** プレイ中（HUD は後続タスク） */
	Play,
	/** クリア（後続タスク） */
	Clear,
};

/**
 * 画面の切り替えと、パズル 1 回分（= セッション）の作成・破棄・作り直しを受け持つ GameMode。
 *
 * 起動するとタイトル / 難易度選択画面（UCubelithTitleWidget）を出し、「開始」で選ばれた難易度とシードで
 * セッションを作る（RULES.md 2 章のコアゲームループ 1〜3）。
 *
 * タイトルの初期選択は **外部指定（Docs/SPEC_UE.md 7.7 の `?seed=` / `?n=` / `?m=` / `?rot=` と
 * コマンドライン）> セーブの「最後に選んだ難易度」（RULES.md 3.8）> 下の UPROPERTY の既定** の順で決まる
 * （Web 版 WebMock/src/main.ts の initialSettings と同じ）。外部指定は検証用の口として残してある。
 *
 * 解釈: ウィジェットの生成・AddToViewport・RemoveFromParent はここ 1 箇所にまとめる
 * （ACubelithPlayerController ではなく GameMode を選んだ）。理由は、どの画面を出すかが
 * 「セッションが有るか・クリアしたか」= この GameMode が持つ状態で決まり、画面の入れ替えと
 * セッションの作り直しを同じ場所で行えると順序の取り違えが起きないため。人が UMG を作ったときに
 * 差し替える口（下の TSubclassOf）も、効果音（SnapSound）と同じ「GameMode の Blueprint 派生」に集まる。
 *
 * PlayerController は ACubelithPlayerController（ピースの選択。U3）、HUD は既定のまま使う。
 */
UCLASS()
class CUBELITH_API ACubelithGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACubelithGameMode();

	/** クリアの仮表示を出し続けるためだけの Tick（クリアしている間だけ有効になる） */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * ゲーム状態（RULES.md 3.3 / 3.4）。セッションが無ければ nullptr。
	 * 所有権は GameMode のまま（TUniquePtr で持つ）なので、呼び出し側は寿命を持たない生ポインタとして読む
	 */
	Cubelith::FGame* GetGame() const { return Game.Get(); }

	/** ピースを描くアクタ。セッションが無ければ nullptr */
	ACubelithPuzzleActor* GetPuzzleActor() const { return PuzzleActor; }

	/**
	 * 配置が変わるたびに呼ばれる（Cubelith::FGame の OnChange からの中継）。
	 * 乗る側は AddUObject で登録すれば、消えたときに自動で飛ばされる（明示的な解除は要らない）。
	 * セッションを作り直してもこの口そのものは付け替えないので登録は残る
	 */
	FCubelithPlacementsChanged OnPlacementsChanged;

	/**
	 * スナップした瞬間の効果音を鳴らす（RULES.md 3.5）。SnapSound が空なら何もしない。
	 * ACubelithPlayerController が吸着を確定させた時点で呼ぶ
	 */
	void PlaySnapSound();

	// --- セッション（パズル 1 回分）---

	/**
	 * タイトル / 難易度選択画面を出す（RULES.md 6 章）。走っているセッションは畳む。
	 * 初期選択とシードは呼び出し側が決めて渡す（起動時は外部指定とセーブから、
	 * 「難易度へ戻る」は直前の難易度とシードの引き直しから。RULES.md 2 章）
	 */
	void ShowTitle(int32 N, int32 M, bool bInAllowRotation, uint32 TitleSeed);

	/**
	 * タイトルへ戻る（RULES.md 2 章の「難易度を変える」「難易度へ戻る」）。
	 * 直前の難易度が選ばれた状態で開き、シードは新しく引き直す
	 */
	void ReturnToTitle();

	/**
	 * パズル 1 回分を始める（RULES.md 2 章の 2〜3）。走っているセッションがあれば畳んでから作り直す。
	 * N は 3..7、M は N ごとのプリセットへ寄せる（RULES.md 3.1）
	 */
	void StartSession(int32 N, int32 M, bool bInAllowRotation, uint32 InSeed);

	/**
	 * 同じ難易度でシードだけ引き直して作り直す（RULES.md 2 章の「もう一度」「次の問題」）。
	 * セッションが走っていなければ直前に選ばれていた難易度で始める
	 */
	void RestartWithNewSeed();

	/** 走っているセッションを畳む（画面は切り替えない）。セッションが無ければ何もしない */
	void EndSession();

	// --- 盤面の操作（HUD は後続タスクで、ここを呼ぶ）---

	/**
	 * ピースの固定を切り替える（RULES.md 3.3「固定」。Web 版 main.ts の onToggleLock）。
	 *
	 * 未固定なら手動の固定（`Cubelith::ELockKind::Manual`）を付け、手動の固定中なら外す。
	 * **ヒントの固定（`ELockKind::Hint`）は解除できない**ので、そのピースでは何もしない。
	 * 固定するときは走っている自由回転を先に確定させ、固定した位置で止めるためにスナップの補間を打ち切る。
	 * セッションが無い / 未知のピース id なら何もしない
	 */
	void ToggleLock(int32 PieceId);

	/**
	 * ヒントを使う（RULES.md 3.7。Web 版 main.ts の onHint）。
	 *
	 * `Cubelith::PickHintPiece` が選んだピースを解答の位置と向きへ置いてから、解除できない
	 * ヒントの固定を付ける（**置いてから固定する**。固定済みには `Place` が効かない）。
	 * 使えるヒントが無いとき（未固定が 1 個以下）は何もしない。選択は解除しない
	 */
	void UseHint();

	/**
	 * ヒントが使えるか（RULES.md 3.7）。HUD がボタンの有効 / 無効に使う。
	 * セッションが無ければ false
	 */
	bool IsHintAvailable() const;

	/**
	 * 同じシードで散らし直す（RULES.md 3.3「やり直し」。Web 版 main.ts の onReset）。
	 *
	 * ヒントで固定したピースは現在の位置に残し、それ以外を散らし直す。手動の固定は解け
	 * （`Cubelith::FGame::Reset` の振る舞い）、ピースの選択も外れ、走っているスナップの補間は
	 * すべて打ち切る。難易度もシードも変えないので、ヒントの固定が無ければ最初の散らしと同じ配置に戻る
	 */
	void ScatterAgain();

	/**
	 * 固定の表示（鍵アイコン。RULES.md 6 章）を、今のゲーム状態のとおりに揃え直す。
	 *
	 * 固定 / 固定解除は配置を変えない ＝ `Cubelith::FGame` の `OnChange` が来ないので、
	 * 固定の状態を変えた操作が自分で呼ぶ。全ピースを見て銀 / 金 / 無しを流し込む
	 * （差分を追わないので、セーブからの復元のように一度に複数変わる経路でもそのまま使える）
	 */
	void RefreshLockIcons();

	/** セッションが走っているか（= 遊べる盤面があるか） */
	bool HasSession() const { return Game.IsValid(); }

	/**
	 * 新しい乱数シードを引く（RULES.md 2 章の「もう一度」「次の問題」「難易度を変える」。
	 * Web 版 WebMock/src/ui/difficulty.ts の randomSeed）
	 */
	static uint32 DrawRandomSeed();

	/** 今出している画面 */
	ECubelithScreen GetActiveScreen() const { return ActiveScreen; }

	/**
	 * 今のセッションの生成結果（RULES.md 3.2 の N / M / シード / ピース / 解答）。
	 * ヒント（Solution が要る）・散らし直し（同じシードが要る）と、後続タスクのセーブ（難易度とシードが要る）が読む。
	 * HasSession が false のときの中身は見ない（ピースが空）
	 */
	const Cubelith::FGeneratedPuzzle& GetSessionPuzzle() const { return SessionPuzzle; }

	/** 今のセッションの空間サイズ N（セッションが無ければ 0） */
	int32 GetSessionSpaceSize() const { return SessionPuzzle.N; }

	/** 今のセッションのピース分割数 M（セッションが無ければ 0） */
	int32 GetSessionPieceCount() const { return SessionPuzzle.M; }

	/** 今のセッションで使ったシード（RULES.md 3.1。「もう一度」の再現とセーブに使う） */
	uint32 GetSessionSeed() const { return SessionPuzzle.Seed; }

	/**
	 * この盤面がパズルの回転「あり」か（RULES.md 3.1）。タイトルで選ばれた値
	 * （起動直後は外部指定 / セーブ / UPROPERTY の値）で、ACubelithPlayerController が回転操作の可否に使う。
	 * セッションが無い間は「なし」（そもそも操作する盤面が無い）
	 */
	bool IsRotationAllowed() const { return bSessionAllowRotation; }

	// --- 人がエディタで差し替えるもの ---

	/**
	 * スナップした瞬間に鳴らす効果音（RULES.md 3.5。本実装は MetaSounds で人が作る。Docs/SPEC_UE.md 4 章）。
	 *
	 * 割り当てが無ければ鳴らさない（警告も出さない。音が無いまま遊べる状態を正常として扱う）。
	 * ここに置いたのは、人がエディタで割り当てられる場所が要るため（`.uasset` は AI が作らない。
	 * Docs/SPEC_UE.md 0 章）。GameMode は Config/DefaultEngine.ini の GlobalDefaultGameMode で
	 * 指されているので、人がこのクラスの Blueprint 派生を作ってそこを差し替えれば音を割り当てられる
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Audio")
	TObjectPtr<USoundBase> SnapSound;

	/**
	 * タイトル / 難易度選択画面のクラス（既定は C++ の UCubelithTitleWidget）。
	 * 人が UMG のウィジェットブループリントを作ったらここを差し替える（手順は Docs/SPEC_UE.md 4 章）
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|UI")
	TSubclassOf<UCubelithScreenWidget> TitleWidgetClass;

	/**
	 * プレイ中の画面（HUD。RULES.md 6 章）のクラス。**後続タスクで足すので既定は空**。
	 * 空のときは「その画面では何も出さない」= 前の画面を外すだけになる
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|UI")
	TSubclassOf<UCubelithScreenWidget> PlayWidgetClass;

	/** クリア画面（RULES.md 6 章）のクラス。**後続タスクで足すので既定は空** */
	UPROPERTY(EditAnywhere, Category = "Cubelith|UI")
	TSubclassOf<UCubelithScreenWidget> ClearWidgetClass;

	/**
	 * 空間サイズ N の既定（RULES.md 3.1。既定 3）。
	 * タイトルの初期選択に使う。`?n=` / `-CubelithN=` とセーブの「最後に選んだ難易度」のほうが優先される。
	 * 範囲外は 3..7 に丸める（Docs/SPEC_UE.md 7.7）
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle", meta = (ClampMin = "3", ClampMax = "7"))
	int32 SpaceSize = 3;

	/**
	 * ピース分割数 M の既定（RULES.md 3.1。N=3 の既定は 4）。
	 * `?m=` / `-CubelithM=` とセーブのほうが優先される。N ごとの 5 段のプリセットのうち最も近いものへ寄せる
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle", meta = (ClampMin = "2", ClampMax = "27"))
	int32 PieceCount = 4;

	/**
	 * パズルの回転の既定（RULES.md 3.1。既定は「なし」= 全ピースを恒等の向きで散らす）。
	 * `?rot=` / `-CubelithRotation=` とセーブのほうが優先される
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle")
	bool bAllowRotation = false;

	/**
	 * 乱数シード。−1（負の値）= 指定なし、0..4294967295 = その値を uint32 として使う。
	 *
	 * 解釈: RULES.md 3.1 のシードは符号なし 32 bit だが、uint32 は UPROPERTY にできないので int64 で持つ。
	 * マップ URL のオプション `?seed=` とコマンドライン引数 `-CubelithSeed=` のほうが優先される
	 * （Docs/SPEC_UE.md 7.7）。どれも無ければランダムに引き、実際に使った値を LogCubelith に出すので、
	 * 同じパズルを再現したいときはその値をここか `?seed=` に入れる。
	 * 効くのは起動時のタイトルの表示だけで、「もう一度」「難易度へ戻る」では毎回引き直す（RULES.md 2 章）。
	 */
	UPROPERTY(EditAnywhere, Category = "Cubelith|Puzzle", meta = (ClampMin = "-1"))
	int64 Seed = -1;

protected:
	/** マップ URL のオプション `?seed=` / `?n=` / `?m=` / `?rot=` を受け取る（Docs/SPEC_UE.md 7.7）。解釈は BeginPlay で行う */
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/**
	 * 画面を切り替える。前の画面を必ず外し（screens.ts の clear）、その画面のクラスが割り当てられていれば
	 * ウィジェットを作って返す。**ビューポートへ出すのは呼び出し側**（作ってから初期値と処理を結んでから
	 * 出したいため）。クラスが空なら前の画面を外すだけで nullptr を返す
	 */
	UCubelithScreenWidget* BeginScreen(ECubelithScreen Screen);

	/** その画面に割り当てられているウィジェットのクラス（無ければ空） */
	TSubclassOf<UCubelithScreenWidget> GetScreenWidgetClass(ECubelithScreen Screen) const;

	/** 「開始」が押されたとき（UCubelithTitleWidget::OnStart から呼ばれる） */
	void HandleTitleStart(const FCubelithTitleSelection& Selection);

	/**
	 * タイトルの初期選択に使う難易度を決める（Docs/SPEC_UE.md 7.7 の優先順位 + セーブ）。
	 * コマンドラインをここで読み、解釈そのものは Cubelith::ResolveDifficulty に任せる。
	 * セーブの「最後に選んだ難易度」は UPROPERTY の既定の代わりに渡す（下の実装の「解釈:」）。
	 * 選んだ値とその経路、無視した指定・寄せた指定をログに出す
	 */
	Cubelith::FDifficultyResolution ResolveDifficulty();

	/**
	 * 起動時に使うシードを決める（Docs/SPEC_UE.md 7.7 の優先順位）。
	 * コマンドラインとランダムの控えをここで用意し、解釈そのものは Cubelith::ResolveSeed に任せる。
	 * 選んだ値とその経路、無視した不正な指定をログに出す
	 */
	uint32 ResolveSeed();

	/**
	 * プレイヤーの Pawn が軌道カメラなら、注視点と距離をパズルに合わせる。合わせられたら true。
	 * Pawn がまだ湧いていない / BeginPlay 前（合わせても初期値に戻される）なら false を返して再試行に回す
	 */
	bool TryFrameCamera();

	/** Pawn がまだ整っていなかったときの再試行（タイマーから呼ぶ） */
	void RetryFrameCamera();

	/** セッションを作るたびにカメラの距離合わせを掛け直す（湧く順に依存するので必要なら再試行へ回す） */
	void StartFrameCamera();

	/**
	 * クリア判定（RULES.md 3.4）の結果を画面とログに反映する。
	 *
	 * 判定そのものは Cubelith::FGame が更新のたびに走らせていて、ここはその結果を受け取るだけ
	 * （判定のロジックは足さない・変えない）。false → true に変わったときに LogCubelith へ 1 行出し、
	 * クリアしている間はずっと見えるよう画面に仮の表示を出す。本実装の UI は後続タスク、演出は U5
	 */
	void UpdateSolvedDisplay(bool bSolved);

	/** 操作しているプレイヤーのコントローラ（ACubelithPlayerController でなければ nullptr） */
	ACubelithPlayerController* GetCubelithPlayerController() const;

	/** クリアの仮表示を出しているか（= 直近に受け取ったクリア判定の結果） */
	bool bSolvedShown = false;

	/**
	 * 今のセッションの生成結果（Pieces と Solution・N / M / シード）。セッションが無ければ既定値。
	 * Cubelith::FGeneratedPuzzle は USTRUCT ではないので素のメンバとして持つ（Docs/SPEC_UE.md 7.1）
	 */
	Cubelith::FGeneratedPuzzle SessionPuzzle;

	/** 今のセッションのパズルの回転（FGeneratedPuzzle に入っていない項目なのでここに持つ） */
	bool bSessionAllowRotation = false;

	/**
	 * 直前に選ばれていた難易度（RULES.md 2 章の「もう一度」「難易度へ戻る」で引き継ぐ）。
	 * ShowTitle と StartSession が更新する。セッションを畳んでも残る
	 */
	int32 LastSpaceSize = 3;
	int32 LastPieceCount = 4;
	bool bLastAllowRotation = false;

	/** 今出している画面 */
	ECubelithScreen ActiveScreen = ECubelithScreen::None;

	/** 今出している画面のウィジェット（出していなければ nullptr） */
	UPROPERTY()
	TObjectPtr<UCubelithScreenWidget> ActiveWidget;

	/** ピースを描くアクタ。セッションを作るたびにワールド原点に湧かせ、畳むときに消す */
	UPROPERTY()
	TObjectPtr<ACubelithPuzzleActor> PuzzleActor;

	/**
	 * ゲーム状態（RULES.md 3.3 / 3.4）。FGame は UObject ではないのでメンバに生で持つ（Docs/SPEC_UE.md 7.1）。
	 * 配置が変わると OnChange から PuzzleActor->UpdatePlacements が呼ばれる
	 */
	TUniquePtr<Cubelith::FGame> Game;

	/** InitGame で読んだマップ URL のオプション `?seed=` の値。未指定なら空文字 */
	FString SeedOptionText;

	/** InitGame で読んだマップ URL のオプション `?n=` の値。未指定なら空文字 */
	FString SpaceSizeOptionText;

	/** InitGame で読んだマップ URL のオプション `?m=` の値。未指定なら空文字 */
	FString PieceCountOptionText;

	/** InitGame で読んだマップ URL のオプション `?rot=` の値。未指定なら空文字 */
	FString RotationOptionText;

	/** TryFrameCamera の再試行タイマー */
	FTimerHandle FrameCameraTimer;

	/** 再試行した回数 */
	int32 FrameCameraAttempts = 0;
};
