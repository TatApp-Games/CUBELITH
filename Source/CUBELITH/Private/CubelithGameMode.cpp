#include "CubelithGameMode.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

#include "CubelithClearWidget.h"
#include "CubelithDifficulty.h"
#include "CubelithHudWidget.h"
#include "CubelithLockOps.h"
#include "CubelithLog.h"
#include "CubelithOrbitPawn.h"
#include "CubelithPlayerController.h"
#include "CubelithProgress.h"
#include "CubelithPuzzleActor.h"
#include "CubelithSave.h"
#include "CubelithSaveGame.h"
#include "CubelithScreenWidget.h"
#include "CubelithSeed.h"
#include "CubelithTitleWidget.h"
#include "Generate.h"
#include "Hint.h"

namespace
{
	/**
	 * マップ URL のオプション名（`?seed=123`）。Web 版の `?seed=`（WebMock/src/ui/params.ts）と同じ書き方に
	 * 揃えてあるので、同じシードを両方に渡せば同じパズルが出る（RULES.md 3.6・Docs/SPEC_UE.md 7.7）
	 */
	const TCHAR* SeedOptionKey = TEXT("seed");

	/**
	 * コマンドライン引数の前置き（`-CubelithSeed=123`）。`-seed=` のような一般的すぎる名前は
	 * エンジンや他の機能とぶつかりうるので前置きを付ける（Docs/SPEC_UE.md 7.7）
	 */
	const TCHAR* SeedCommandLineKey = TEXT("CubelithSeed=");

	/**
	 * 難易度のマップ URL のオプション名（`?n=4&m=8&rot=1`）。N と M は Web 版の URL クエリ
	 * （WebMock/src/ui/params.ts の `?n=` / `?m=`）と同じ名前にしてあるので、同じ URL の一部をそのまま渡せる。
	 * 解釈: 「パズルの回転」は Web 版の URL クエリに無い項目なので、UE 版で `?rot=` と決めた（Docs/SPEC_UE.md 7.7）
	 */
	const TCHAR* SpaceSizeOptionKey = TEXT("n");
	const TCHAR* PieceCountOptionKey = TEXT("m");
	const TCHAR* RotationOptionKey = TEXT("rot");

	/**
	 * 難易度のコマンドライン引数の前置き（`-CubelithN=4 -CubelithM=8 -CubelithRotation=1`）。
	 * `-n=` のような一般的すぎる名前を避けるのは `-CubelithSeed=` と同じ理由（Docs/SPEC_UE.md 7.7）
	 */
	const TCHAR* SpaceSizeCommandLineKey = TEXT("CubelithN=");
	const TCHAR* PieceCountCommandLineKey = TEXT("CubelithM=");
	const TCHAR* RotationCommandLineKey = TEXT("CubelithRotation=");

	/** コマンドライン引数を 1 つ読む。付いていなければ空文字（= 未指定）を返す */
	FString ReadCommandLineValue(const TCHAR* Key)
	{
		FString Value;
		if (!FParse::Value(FCommandLine::Get(), Key, Value))
		{
			Value.Empty();
		}
		return Value;
	}

	/**
	 * TryFrameCamera の再試行の間隔（秒）と上限。上限に達したら諦めて警告を出す。
	 * 間隔をフレーム時間より短くしてあるのは、待つ相手（Pawn の生成と BeginPlay）が整った次の
	 * フレームで合わせて、初期の距離のままの絵が何フレームも映らないようにするため
	 * （FTimerManager は繰り返しのタイマーを 1 ティックに 1 回しか撃たないので、実質「毎フレーム試す」になる）。
	 */
	constexpr float FrameCameraRetryIntervalSeconds = 0.01f;
	constexpr int32 MaxFrameCameraAttempts = 300;

	/** ログに出す画面の名前 */
	const TCHAR* ScreenToText(ECubelithScreen Screen)
	{
		switch (Screen)
		{
		case ECubelithScreen::Title: return TEXT("タイトル");
		case ECubelithScreen::Play: return TEXT("プレイ中");
		case ECubelithScreen::Clear: return TEXT("クリア");
		default: return TEXT("なし");
		}
	}
}

ACubelithGameMode::ACubelithGameMode()
{
	// Tick は回さない（U3 でクリアの仮表示を出し続けるためだけに回していたのを、クリア画面を入れたときに外した）
	PrimaryActorTick.bCanEverTick = false;

	// マップ /Game/Maps/Main に PlayerStart が無ければ Pawn はワールド原点に湧く。軌道カメラの注視点は
	// 立方体の中心（= パズルのアクタを置くワールド原点）なのでそれでよく、マップには手を入れない
	DefaultPawnClass = ACubelithOrbitPawn::StaticClass();

	// クリック / タップでピースを選ぶコントローラ（RULES.md 3.3）。Blueprint の .uasset を作らないので
	// ここで C++ のクラスを指す（DefaultPawnClass と同じ書き方。Docs/SPEC_UE.md 0 章）
	PlayerControllerClass = ACubelithPlayerController::StaticClass();

	// 画面も同じ理由で C++ のクラスを既定にする。人が UMG のウィジェットブループリントを作ったら
	// この GameMode の Blueprint 派生でここを差し替える（Docs/SPEC_UE.md 4 章）
	TitleWidgetClass = UCubelithTitleWidget::StaticClass();
	PlayWidgetClass = UCubelithHudWidget::StaticClass();
	ClearWidgetClass = UCubelithClearWidget::StaticClass();
}

void ACubelithGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// マップ URL（`open /Game/Maps/Main?seed=123&n=4&m=8&rot=1`・起動引数のマップ指定）のオプションは
	// ここでしか受け取れない。解釈は BeginPlay の ResolveSeed / ResolveDifficulty でまとめて行う。
	// 未指定なら ParseOption は空文字を返す
	SeedOptionText = UGameplayStatics::ParseOption(Options, SeedOptionKey);
	SpaceSizeOptionText = UGameplayStatics::ParseOption(Options, SpaceSizeOptionKey);
	PieceCountOptionText = UGameplayStatics::ParseOption(Options, PieceCountOptionKey);
	RotationOptionText = UGameplayStatics::ParseOption(Options, RotationOptionKey);
}

void ACubelithGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 起動時はタイトルを出す（パズルは「開始」を押してから作る。RULES.md 2 章のコアゲームループ 1）。
	// 初期選択は 外部指定（Docs/SPEC_UE.md 7.7）> セーブの「最後に選んだ難易度」> UPROPERTY の既定 の順で、
	// シードは外部指定があればそれ、無ければ引き直し（シードは保存しない。RULES.md 3.8）
	const Cubelith::FDifficultyResolution Difficulty = ResolveDifficulty();
	const uint32 InitialSeed = ResolveSeed();

	ShowTitle(Difficulty.SpaceSize, Difficulty.PieceCount, Difficulty.bAllowRotation, InitialSeed);
}

void ACubelithGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// セッション（タイマー・ピースのアクタ・FGame・クリアの状態）をまとめて畳む。
	// FGame の OnChange は弱参照越しなので、畳む前に呼ばれても消えた GameMode / アクタには触らない
	EndSession();

	// 画面も残さない（レベルを開き直したときにウィジェットが積み上がらないように）
	if (ActiveWidget != nullptr)
	{
		ActiveWidget->RemoveFromParent();
		ActiveWidget = nullptr;
	}
	ActiveScreen = ECubelithScreen::None;

	Super::EndPlay(EndPlayReason);
}

void ACubelithGameMode::ShowTitle(int32 N, int32 M, bool bInAllowRotation, uint32 TitleSeed)
{
	// タイトルにいる間はパズルを持たない（Web 版 main.ts の showTitle が disposeSession を呼ぶのと同じ）
	EndSession();

	// 初期選択は必ず有効な値にしてから渡す（プリセット外の M はタイトルで寄せ直されるが、
	// 「直前の難易度」としても使うのでここで揃えておく）
	LastSpaceSize = FMath::Clamp(N, Cubelith::MinSpaceSize, Cubelith::MaxSpaceSize);
	LastPieceCount = Cubelith::NearestPreset(Cubelith::PiecePresets(LastSpaceSize), M);
	bLastAllowRotation = bInAllowRotation;

	UCubelithScreenWidget* const Widget = BeginScreen(ECubelithScreen::Title);
	if (Widget == nullptr)
	{
		return;
	}

	UCubelithTitleWidget* const Title = Cast<UCubelithTitleWidget>(Widget);
	if (Title == nullptr)
	{
		// 人が TitleWidgetClass を別系統のウィジェットに差し替えた。初期選択も「開始」も結べないので出さない
		UE_LOG(LogCubelith, Error,
			TEXT("TitleWidgetClass（%s）が UCubelithTitleWidget の派生ではないのでタイトルを出せない"),
			*Widget->GetClass()->GetName());
		ActiveWidget = nullptr;
		return;
	}

	Title->SetInitialSelection(LastSpaceSize, LastPieceCount, bLastAllowRotation, TitleSeed);
	// 「開始」で押された時点の選択を受け取る（画面は値を持つだけで、セッションを作るのはこの GameMode）
	Title->OnStart.BindUObject(this, &ACubelithGameMode::HandleTitleStart);
	Title->AddToViewport();

	UE_LOG(LogCubelith, Log,
		TEXT("タイトル / 難易度選択画面を出した: 初期選択 N=%d, M=%d, パズルの回転=%s, seed=%u"),
		LastSpaceSize, LastPieceCount, bLastAllowRotation ? TEXT("あり") : TEXT("なし"), TitleSeed);
}

void ACubelithGameMode::ReturnToTitle()
{
	// 直前の難易度が選ばれた状態で開き、シードは新しく引き直す（RULES.md 2 章）
	ShowTitle(LastSpaceSize, LastPieceCount, bLastAllowRotation, DrawRandomSeed());
}

void ACubelithGameMode::StartSession(int32 N, int32 M, bool bInAllowRotation, uint32 InSeed)
{
	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 走っている盤面を先に畳む（ピースのアクタもゲーム状態も 1 セッションに 1 つ）
	EndSession();

	// 範囲外・プリセット外は丸める（Cubelith::GeneratePuzzle は範囲外の N / M を受け取れない）。
	// 外部指定の丸めは Cubelith::ResolveDifficulty が済ませているので、ここは
	// 「タイトル以外から呼ばれたとき」の最後の砦
	const int32 SessionN = FMath::Clamp(N, Cubelith::MinSpaceSize, Cubelith::MaxSpaceSize);
	const int32 SessionM = Cubelith::NearestPreset(Cubelith::PiecePresets(SessionN), M);

	LastSpaceSize = SessionN;
	LastPieceCount = SessionM;
	bLastAllowRotation = bInAllowRotation;
	// 回転操作の可否をコントローラから読めるようにする（ACubelithPlayerController::IsRotationAllowed）
	bSessionAllowRotation = bInAllowRotation;

	// 生成（RULES.md 3.2）。結果（ピースと解答）はセッションの間ずっと持つ
	// （後続タスクのヒントが Solution を、散らし直しが同じシードを読む）
	SessionPuzzle = Cubelith::GeneratePuzzle(SessionN, SessionM, InSeed);

	// 初期散らし（RULES.md 3.2-5）。まだ固定は何も無いので Keep は空
	// （ヒントで固定したピースを残すのは散らし直し。ScatterAgain が同じシードで呼び直す）
	Cubelith::FScatterOptions ScatterOptions;
	ScatterOptions.bAllowRotation = bInAllowRotation;
	const TArray<Cubelith::FPlacement> Scattered =
		Cubelith::ScatterPlacements(SessionPuzzle.Pieces, SessionN, InSeed, ScatterOptions);

	// 配置が変わったら描画に反映する（Game.h の設計どおり）。FGame の寿命は GameMode のメンバとして持つが、
	// コールバックが GameMode より長生きしても壊れないよう弱参照で握る
	TWeakObjectPtr<ACubelithGameMode> WeakThis(this);
	Game = MakeUnique<Cubelith::FGame>(SessionPuzzle.Pieces, SessionN, Scattered,
		[WeakThis](TArrayView<const Cubelith::FPlacement> Placements, bool bSolved)
		{
			ACubelithGameMode* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			if (ACubelithPuzzleActor* Actor = Self->PuzzleActor.Get())
			{
				Actor->UpdatePlacements(Placements);
			}
			// クリア判定は FGame が更新のたびに走らせている（RULES.md 3.4）。ここは結果を受け取るだけ
			Self->HandleSolvedChanged(bSolved);
			// 操作側（ACubelithPlayerController）へ中継する。スナップ候補の発光を計算し直すきっかけ
			// （RULES.md 5.1。毎フレームは回さず、配置が変わったこのタイミングだけで見る）
			Self->OnPlacementsChanged.Broadcast(Placements);
			// 残りピース数とヒントが使えるか（RULES.md 6 章 / 3.7）は配置で変わるので HUD も揃える
			Self->RefreshHud();
		});

	// ピースを描くアクタ。原点に置くので、アクタの原点 = 解答空間の中心 = 軌道カメラの注視点になる
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PuzzleActor = World->SpawnActor<ACubelithPuzzleActor>(
		ACubelithPuzzleActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (PuzzleActor == nullptr)
	{
		UE_LOG(LogCubelith, Error, TEXT("ACubelithPuzzleActor を湧かせられなかったのでセッションを始められない"));
		// 半分だけ出来た状態を残さない（画面はタイトルのまま）
		EndSession();
		return;
	}

	PuzzleActor->Build(Game->Pieces(), SessionN);
	PuzzleActor->UpdatePlacements(Game->Placements());

	UE_LOG(LogCubelith, Log, TEXT("パズルを開始した: N=%d, M=%d, パズルの回転=%s, seed=%u, ピース数=%d"),
		SessionN, SessionM, bInAllowRotation ? TEXT("あり") : TEXT("なし"), InSeed, SessionPuzzle.Pieces.Num());

	// 軌道カメラの距離合わせはセッションを作るたびに掛け直す（N が変われば収める大きさも変わる）
	StartFrameCamera();

	// プレイ中の画面（HUD。RULES.md 6 章）へ
	if (UCubelithScreenWidget* const Widget = BeginScreen(ECubelithScreen::Play))
	{
		if (UCubelithHudWidget* const Hud = Cast<UCubelithHudWidget>(Widget))
		{
			BindHud(*Hud);
			// 回転モードのトグルを出すかは難易度で決まる（RULES.md 6 章）
			Hud->SetAllowRotation(bSessionAllowRotation);
			Hud->AddToViewport();
			// 残りピース数などの初期表示（選択はまだ無い）
			RefreshHud();
		}
		else
		{
			// 人が PlayWidgetClass を別系統のウィジェットに差し替えた。ボタンを結べないので出さない
			UE_LOG(LogCubelith, Error,
				TEXT("PlayWidgetClass（%s）が UCubelithHudWidget の派生ではないので HUD を出せない"),
				*Widget->GetClass()->GetName());
			ActiveWidget = nullptr;
		}
	}

	// FGame は構築時にクリア判定を 1 回走らせるが OnChange は呼ばない。散らした直後は普通クリアではないものの、
	// 初期状態も同じ経路に通しておく（前のセッションのクリアの状態が残らないようにするため）。
	// **HUD を出した後に通す**ので、万一散らした直後がクリアならクリア画面が HUD を置き換えて残る
	HandleSolvedChanged(Game->Solved());
}

void ACubelithGameMode::ToggleLock(int32 PieceId)
{
	if (!Game.IsValid() || PieceId == INDEX_NONE)
	{
		return;
	}

	// 未知のピース id を Cubelith::FGame へ渡すと checkf で落ちるので、配置の有無で先に弾く
	if (Game->PlacementOf(PieceId) == nullptr)
	{
		UE_LOG(LogCubelith, Warning, TEXT("未知のピース id %d の固定を切り替えようとした"), PieceId);
		return;
	}

	const Cubelith::ELockToggleAction Action = Cubelith::DecideLockToggle(Game->LockKindOf(PieceId));
	if (Action == Cubelith::ELockToggleAction::Blocked)
	{
		// ヒントで置いたピースの固定は解除できない（RULES.md 3.3）。HUD はボタンを押せなくするが、
		// ここでも弾いて「押せてしまっても外れない」ようにしておく
		UE_LOG(LogCubelith, Log, TEXT("ピース %d はヒントの固定なので解除できない（RULES.md 3.3）"), PieceId);
		return;
	}

	ACubelithPlayerController* const PlayerController = GetCubelithPlayerController();

	if (Action == Cubelith::ELockToggleAction::Lock)
	{
		// 固定すると回転もできなくなるので、回転モードを抜けて（走っている自由回転を確定させて）から固定する
		// （main.ts の onToggleLock が exitRotateMode を通すのと同じ順）
		if (PlayerController != nullptr)
		{
			PlayerController->ExitRotateMode();
		}

		Game->Lock(PieceId, Cubelith::ELockKind::Manual);

		// 固定した位置で止めるので、走っているスナップの補間は打ち切る（表示だけが後から動かない）
		if (PlayerController != nullptr)
		{
			PlayerController->CancelSnapMotionFor(PieceId);
		}
	}
	else
	{
		Game->Unlock(PieceId);
	}

	// 固定 / 固定解除は配置を変えない ＝ FGame の OnChange が来ないので、派生する表示はここで揃える
	// （鍵アイコンと、固定中は出さないスナップ候補の発光。RULES.md 5.1 / 6 章）
	RefreshLockIcons();
	if (PlayerController != nullptr)
	{
		PlayerController->RefreshSnapHint();
	}
	// 固定のラベル（固定 / 固定解除）と回転モードのトグルを押せるかが変わる（RULES.md 6 章）
	RefreshHud();

	UE_LOG(LogCubelith, Log, TEXT("ピース %d を%s（RULES.md 3.3）"),
		PieceId, (Action == Cubelith::ELockToggleAction::Lock) ? TEXT("手動で固定した") : TEXT("固定解除した"));
}

void ACubelithGameMode::UseHint()
{
	if (!Game.IsValid())
	{
		return;
	}

	// LockedIds は毎回作られる配列なので、TArrayView に渡す間だけ生かしておく
	const TArray<int32> LockedIds = Game->LockedIds();
	const TOptional<int32> PieceId =
		Cubelith::PickHintPiece(Game->Placements(), SessionPuzzle.Solution, LockedIds);
	if (!PieceId.IsSet())
	{
		// 未固定が 1 個以下（RULES.md 3.7）。HUD はボタンを無効にするが、ここでも何もしない
		UE_LOG(LogCubelith, Log, TEXT("使えるヒントが無い（未固定のピースが 1 個以下。RULES.md 3.7）"));
		return;
	}

	const Cubelith::FPlacement* const Answer = SessionPuzzle.Solution.FindByPredicate(
		[TargetId = PieceId.GetValue()](const Cubelith::FPlacement& Placement) { return Placement.PieceId == TargetId; });
	if (Answer == nullptr)
	{
		UE_LOG(LogCubelith, Error, TEXT("ピース %d の解答配置が無いのでヒントを使えない"), PieceId.GetValue());
		return;
	}

	ACubelithPlayerController* const PlayerController = GetCubelithPlayerController();

	// ヒントで置くピースが自由回転中なら、そのねじれを先に片付ける（main.ts と同じ）
	if (PlayerController != nullptr)
	{
		PlayerController->CommitFreeRotation();
	}

	// 解答の位置と向きへ置いてから固定する（**この順が要る**。Place は固定済みのピースに効かない）。
	// Place の OnChange で配置・クリア判定・スナップ候補は反映される
	Game->Place(PieceId.GetValue(), Answer->Orientation, Answer->Position);
	Game->Lock(PieceId.GetValue(), Cubelith::ELockKind::Hint);

	// 送った先で止めるので、走っているスナップの補間は打ち切る
	if (PlayerController != nullptr)
	{
		PlayerController->CancelSnapMotionFor(PieceId.GetValue());
	}

	// 固定を付けたのは Place の後なので、鍵アイコンと候補の発光はここで揃え直す（ToggleLock と同じ理由）
	RefreshLockIcons();
	if (PlayerController != nullptr)
	{
		PlayerController->RefreshSnapHint();
	}
	// ヒントを使うと未固定が 1 つ減る ＝ ボタンが使えなくなることがある（RULES.md 3.7）。
	// 選択中のピースをヒントで固定したときは「固定解除」も押せなくなる（RULES.md 6 章）
	RefreshHud();

	UE_LOG(LogCubelith, Log,
		TEXT("ヒントでピース %d を解答へ送って固定した: 向き %d, 位置 (%d, %d, %d)（RULES.md 3.7）"),
		PieceId.GetValue(), Answer->Orientation, Answer->Position.X, Answer->Position.Y, Answer->Position.Z);
}

bool ACubelithGameMode::IsHintAvailable() const
{
	if (!Game.IsValid())
	{
		return false;
	}

	return Cubelith::IsHintAvailable(Game->Pieces().Num(), Game->LockedIds().Num());
}

void ACubelithGameMode::ScatterAgain()
{
	if (!Game.IsValid())
	{
		return;
	}

	ACubelithPlayerController* const PlayerController = GetCubelithPlayerController();
	if (PlayerController != nullptr)
	{
		// 散らす前に回転モードを抜けて表示上のねじれを片付ける（main.ts の onReset と同じ）
		PlayerController->ExitRotateMode();
		// ピースの選択も外れる（RULES.md 3.3「やり直し」）
		PlayerController->SetSelectedPiece(INDEX_NONE);
		// 走っているスナップの補間はすべて打ち切る（散らした先から表示だけが元へ戻っていかないように）
		PlayerController->CancelAllSnapMotion();
	}

	Cubelith::FScatterOptions ScatterOptions;
	ScatterOptions.bAllowRotation = bSessionAllowRotation;
	// ヒントで固定したピースはその位置に残す（RULES.md 3.3。残すのは「現在の」配置。CubelithLockOps.h の「解釈:」）
	ScatterOptions.Keep = Cubelith::CollectHintKeptPlacements(Game->Placements(),
		[this](int32 PieceId) { return Game->LockKindOf(PieceId); });

	// **同じシード**で散らし直す（RULES.md 3.3。ヒントの固定が無ければ最初の散らしと同じ配置に戻る）
	const TArray<Cubelith::FPlacement> Scattered = Cubelith::ScatterPlacements(
		SessionPuzzle.Pieces, SessionPuzzle.N, SessionPuzzle.Seed, ScatterOptions);

	// Reset は手動の固定を解き、ヒントの固定は残す。OnChange が回るので配置・クリアの表示・
	// スナップ候補（と後続タスクで繋ぐ残りピース数）は実際の状態へ揃う
	Game->Reset(Scattered);

	// 手動の固定が解けた分の鍵アイコンは配置の変化では分からないので、ここで揃え直す
	RefreshLockIcons();
	// 選択が外れ固定も解けたので HUD も揃える（Reset の OnChange では固定の解除が反映済みか見えない）
	RefreshHud();

	UE_LOG(LogCubelith, Log, TEXT("散らし直した: seed=%u, ヒントで残したピース=%d 個（RULES.md 3.3）"),
		SessionPuzzle.Seed, ScatterOptions.Keep.Num());
}

void ACubelithGameMode::RefreshHud()
{
	// HUD を出していない（タイトル / クリア）ときは何もしない。呼ぶ側が画面を気にしなくてよいように、
	// 判定はここ 1 か所に集める
	UCubelithHudWidget* const Hud = Cast<UCubelithHudWidget>(ActiveWidget.Get());
	if (Hud == nullptr || !Game.IsValid())
	{
		return;
	}

	const ACubelithPlayerController* const PlayerController = GetCubelithPlayerController();
	const int32 SelectedPieceId =
		(PlayerController != nullptr) ? PlayerController->GetSelectedPieceId() : INDEX_NONE;

	// 順番は Web 版 main.ts と同じ（選択 → 固定 → 回転モード）。選択を差し替えると固定と回転モードは
	// 一旦初期値へ戻るので、その後に今の値を流し込む
	Hud->SetSelected(SelectedPieceId);
	Hud->SetLock((SelectedPieceId != INDEX_NONE)
		? Game->LockKindOf(SelectedPieceId) : TOptional<Cubelith::ELockKind>());
	Hud->SetRotateMode((PlayerController != nullptr) && PlayerController->IsRotateModeOn());

	// 残りピース数（RULES.md 6 章）。数え方は CubelithProgress.h の「解釈:」
	Hud->SetRemaining(
		Cubelith::UnsettledPieceCount(Game->Pieces(), Game->Placements(), Game->N()),
		Game->Pieces().Num());

	Hud->SetHintEnabled(IsHintAvailable());
}

void ACubelithGameMode::BindHud(UCubelithHudWidget& Hud)
{
	// 押されたことだけを受け取り、盤面を触るのはこの GameMode（Docs/SPEC_UE.md 4 章の「画面（UI）」）
	Hud.OnToggleRotateMode.BindUObject(this, &ACubelithGameMode::HandleHudToggleRotateMode);
	Hud.OnToggleLock.BindUObject(this, &ACubelithGameMode::HandleHudToggleLock);
	Hud.OnScatterAgain.BindUObject(this, &ACubelithGameMode::ScatterAgain);
	Hud.OnHint.BindUObject(this, &ACubelithGameMode::UseHint);
	// 「次の問題」は難易度はそのままシードだけ引き直す、「難易度へ戻る」はタイトルへ戻る（RULES.md 2 章）
	Hud.OnNext.BindUObject(this, &ACubelithGameMode::RestartWithNewSeed);
	Hud.OnBackToTitle.BindUObject(this, &ACubelithGameMode::ReturnToTitle);
}

void ACubelithGameMode::HandleHudToggleLock()
{
	ACubelithPlayerController* const PlayerController = GetCubelithPlayerController();
	if (PlayerController == nullptr)
	{
		return;
	}

	// 対象は選択中のピース（HUD はピースを選んでいるときだけこのボタンを出す。RULES.md 6 章）
	const int32 PieceId = PlayerController->GetSelectedPieceId();
	if (PieceId == INDEX_NONE)
	{
		return;
	}

	ToggleLock(PieceId);
}

void ACubelithGameMode::HandleHudToggleRotateMode()
{
	ACubelithPlayerController* const PlayerController = GetCubelithPlayerController();
	if (PlayerController == nullptr)
	{
		return;
	}

	// 入れる / 抜けるを決めるのはコントローラ（選択が無い / 固定中 / 回転「なし」では入らない）。
	// その結果を HUD のラベルへ返す（main.ts の onToggleRotateMode と同じ形）
	PlayerController->ToggleRotateMode();
	RefreshHud();
}

void ACubelithGameMode::RefreshLockIcons()
{
	if (!Game.IsValid() || PuzzleActor == nullptr)
	{
		return;
	}

	// 全ピースに今の状態を流す。ACubelithPuzzleActor::SetLockIcon は変わらないピースでは何もしないので、
	// まとめて流し込んでも余計な書き直しは起きない
	for (const Cubelith::FPiece& Piece : Game->Pieces())
	{
		PuzzleActor->SetLockIcon(Piece.Id, Game->LockKindOf(Piece.Id));
	}
}

ACubelithPlayerController* ACubelithGameMode::GetCubelithPlayerController() const
{
	return Cast<ACubelithPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
}

void ACubelithGameMode::RestartWithNewSeed()
{
	// 難易度はそのままにシードだけ新しく引き直して生成し直す（RULES.md 2 章）
	StartSession(LastSpaceSize, LastPieceCount, bLastAllowRotation, DrawRandomSeed());
}

void ACubelithGameMode::EndSession()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FrameCameraTimer);
	}
	FrameCameraAttempts = 0;

	// 前の盤面の選択・ドラッグ・スナップは新しい盤面では意味が無い（同じ N / M でも形が変わる）
	if (ACubelithPlayerController* const PlayerController =
		Cast<ACubelithPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
	{
		PlayerController->ResetForNewSession();
	}

	// クリアの状態を落とす（次のセッションでもう一度クリアしたときに、変わり目としてまた拾えるように）。
	// 画面はここでは切り替えない（呼び出し元の ShowTitle / StartSession が続けて切り替える）
	HandleSolvedChanged(false);

	// FGame を先に畳む（OnChange からピースのアクタを触るので、アクタを消す前に止める）
	Game.Reset();

	if (PuzzleActor != nullptr)
	{
		// EndPlay（ワールドの片付け）から来たときは既に消えていることがある
		if (IsValid(PuzzleActor))
		{
			PuzzleActor->Destroy();
		}
		PuzzleActor = nullptr;
	}

	// 「セッションが無い」= ピースが空。N / M / シードも既定へ戻す
	SessionPuzzle = Cubelith::FGeneratedPuzzle();
	bSessionAllowRotation = false;
}

uint32 ACubelithGameMode::DrawRandomSeed()
{
	// 起動ごと・引くごとに変える必要があるが、FMath::Rand はプラットフォームによって 15 bit しか
	// 返さないので、時刻で種を撒いた FRandomStream から 32 bit まるごと取る。
	// 同じ刻み（100 ns）のうちに 2 回引かれても同じ値にならないよう、引いた回数も種に混ぜる
	static int32 DrawCount = 0;
	++DrawCount;

	const int64 Mixed = FDateTime::UtcNow().GetTicks() + static_cast<int64>(DrawCount) * 2654435761LL;
	const FRandomStream Stream(static_cast<int32>(Mixed & static_cast<int64>(MAX_int32)));
	return Stream.GetUnsignedInt();
}

UCubelithScreenWidget* ACubelithGameMode::BeginScreen(ECubelithScreen Screen)
{
	// 前の画面は必ず外す（Web 版 screens.ts の clear。ウィジェットとリスナが積み上がらない）
	if (ActiveWidget != nullptr)
	{
		ActiveWidget->RemoveFromParent();
		ActiveWidget = nullptr;
	}
	ActiveScreen = Screen;

	const TSubclassOf<UCubelithScreenWidget> WidgetClass = GetScreenWidgetClass(Screen);
	if (WidgetClass == nullptr)
	{
		// その画面のクラスがまだ無い（後続タスクで足す HUD / クリア画面）。前の画面を外すだけ
		return nullptr;
	}

	APlayerController* const PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (PlayerController == nullptr)
	{
		UE_LOG(LogCubelith, Warning,
			TEXT("PlayerController が無いので %s の画面を出せない"), ScreenToText(Screen));
		return nullptr;
	}

	UCubelithScreenWidget* const Widget = CreateWidget<UCubelithScreenWidget>(PlayerController, WidgetClass);
	if (Widget == nullptr)
	{
		UE_LOG(LogCubelith, Error,
			TEXT("%s の画面（%s）を作れなかった"), ScreenToText(Screen), *WidgetClass->GetName());
		return nullptr;
	}

	ActiveWidget = Widget;
	return Widget;
}

TSubclassOf<UCubelithScreenWidget> ACubelithGameMode::GetScreenWidgetClass(ECubelithScreen Screen) const
{
	switch (Screen)
	{
	case ECubelithScreen::Title: return TitleWidgetClass;
	case ECubelithScreen::Play: return PlayWidgetClass;
	case ECubelithScreen::Clear: return ClearWidgetClass;
	default: return nullptr;
	}
}

void ACubelithGameMode::HandleTitleStart(const FCubelithTitleSelection& Selection)
{
	// タイトルで選ばれている値で始める（外部指定は初期選択にだけ効く。Docs/SPEC_UE.md 7.7）
	StartSession(Selection.SpaceSize, Selection.PieceCount, Selection.bAllowRotation, Selection.Seed);
}

uint32 ACubelithGameMode::ResolveSeed()
{
	// コマンドライン引数 `-CubelithSeed=<0..4294967295>`。値が付いていなければ空文字のまま（= 未指定）
	const FString CommandLineText = ReadCommandLineValue(SeedCommandLineKey);

	// どれも指定が無いときのために引いておく
	const uint32 RandomSeed = DrawRandomSeed();

	const Cubelith::FSeedResolution Resolution =
		Cubelith::ResolveSeed(SeedOptionText, CommandLineText, Seed, RandomSeed);

	// 打ち間違いに気付けるよう、無視した指定を 1 行にまとめて出す
	if (Resolution.IgnoredInputs.Num() > 0)
	{
		UE_LOG(LogCubelith, Warning,
			TEXT("シードの指定が不正なので無視した: %s（受け付けるのは 0..4294967295 の 10 進整数だけ）"),
			*FString::Join(Resolution.IgnoredInputs, TEXT("、")));
	}

	// 人が同じパズルを再現できるよう、使った値と経路を必ず残す
	UE_LOG(LogCubelith, Log, TEXT("シードを %s から決めた: %u（再現するには ?seed=%u か -CubelithSeed=%u）"),
		Cubelith::SeedSourceToText(Resolution.Source), Resolution.Seed, Resolution.Seed, Resolution.Seed);

	return Resolution.Seed;
}

Cubelith::FDifficultyResolution ACubelithGameMode::ResolveDifficulty()
{
	// 解釈: セーブの「最後に選んだ難易度」（RULES.md 3.8）は、UPROPERTY の既定値の代わりに
	// Cubelith::ResolveDifficulty へ渡す。こうすると優先順位が
	// **外部指定 > セーブ > UPROPERTY の既定** になり（Web 版 WebMock/src/main.ts の initialSettings と同じ）、
	// 解釈の純粋関数とそのテスト（CUBELITH.Render.Difficulty.*）を触らずに済む。
	// セーブが無いときだけ UPROPERTY が効くよう、スロットの有無を先に見る
	int32 DefaultSpaceSize = SpaceSize;
	int32 DefaultPieceCount = PieceCount;
	bool bDefaultAllowRotation = bAllowRotation;
	if (UGameplayStatics::DoesSaveGameExist(FString(Cubelith::SaveSlotName), Cubelith::SaveUserIndex))
	{
		const FCubelithSaveData Saved = Cubelith::LoadSaveData();
		DefaultSpaceSize = Saved.Difficulty.SpaceSize;
		DefaultPieceCount = Saved.Difficulty.PieceCount;
		bDefaultAllowRotation = Saved.Difficulty.bAllowRotation;

		UE_LOG(LogCubelith, Log,
			TEXT("セーブの「最後に選んだ難易度」を初期選択の既定にする: N=%d, M=%d, パズルの回転=%s"),
			DefaultSpaceSize, DefaultPieceCount, bDefaultAllowRotation ? TEXT("あり") : TEXT("なし"));
	}

	const Cubelith::FDifficultyResolution Resolution = Cubelith::ResolveDifficulty(
		SpaceSizeOptionText, PieceCountOptionText, RotationOptionText,
		ReadCommandLineValue(SpaceSizeCommandLineKey),
		ReadCommandLineValue(PieceCountCommandLineKey),
		ReadCommandLineValue(RotationCommandLineKey),
		DefaultSpaceSize, DefaultPieceCount, bDefaultAllowRotation);

	// 打ち間違いに気付けるよう、読めなかった指定を 1 行にまとめて出す（シードと同じ）
	if (Resolution.IgnoredInputs.Num() > 0)
	{
		UE_LOG(LogCubelith, Warning,
			TEXT("難易度の指定が読めないので無視した: %s")
			TEXT("（N と M は 10 進整数、パズルの回転は 1/true/on/yes か 0/false/off/no）"),
			*FString::Join(Resolution.IgnoredInputs, TEXT("、")));
	}

	// 範囲外・プリセット外は無視ではなく丸めるので、丸めたことが分かるように別の行で出す
	if (Resolution.AdjustedInputs.Num() > 0)
	{
		UE_LOG(LogCubelith, Warning,
			TEXT("難易度の指定を有効な値へ寄せた: %s（N は %d..%d、M は N ごとの 5 段のプリセット。RULES.md 3.1）"),
			*FString::Join(Resolution.AdjustedInputs, TEXT("、")),
			Cubelith::MinSpaceSize, Cubelith::MaxSpaceSize);
	}

	// 人が同じ盤面を開き直せるよう、使った値と経路を必ず残す
	// （経路の「プロパティ」は、セーブがあればセーブの値のこと。上の行で何を渡したか出している）
	UE_LOG(LogCubelith, Log,
		TEXT("難易度を決めた: N=%d（%s）, M=%d（%s）, パズルの回転=%s（%s）")
		TEXT("（再現するには ?n=%d&m=%d&rot=%d か -CubelithN=%d -CubelithM=%d -CubelithRotation=%d）"),
		Resolution.SpaceSize, Cubelith::DifficultySourceToText(Resolution.SpaceSizeSource),
		Resolution.PieceCount, Cubelith::DifficultySourceToText(Resolution.PieceCountSource),
		Resolution.bAllowRotation ? TEXT("あり") : TEXT("なし"),
		Cubelith::DifficultySourceToText(Resolution.RotationSource),
		Resolution.SpaceSize, Resolution.PieceCount, Resolution.bAllowRotation ? 1 : 0,
		Resolution.SpaceSize, Resolution.PieceCount, Resolution.bAllowRotation ? 1 : 0);

	return Resolution;
}

void ACubelithGameMode::StartFrameCamera()
{
	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Pawn の生成順に依存するので、まだ湧いていなければ湧くまで短い間隔で試し直す
	if (TryFrameCamera())
	{
		return;
	}

	FrameCameraAttempts = 0;
	World->GetTimerManager().SetTimer(FrameCameraTimer, this, &ACubelithGameMode::RetryFrameCamera,
		FrameCameraRetryIntervalSeconds, /*bLoop=*/true);
}

bool ACubelithGameMode::TryFrameCamera()
{
	const UWorld* World = GetWorld();
	if (World == nullptr || PuzzleActor == nullptr)
	{
		return false;
	}

	const double Radius = PuzzleActor->GetBoundingRadiusCm();
	if (!(Radius > 0.0))
	{
		// まだ何も置いていない（= 合わせる対象が無い）。再試行しても変わらないので諦める
		return true;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		if (PlayerController == nullptr)
		{
			continue;
		}

		ACubelithOrbitPawn* OrbitPawn = Cast<ACubelithOrbitPawn>(PlayerController->GetPawn());
		if (OrbitPawn == nullptr)
		{
			continue;
		}

		// Pawn の BeginPlay（ResetToInitial）は距離・向きを初期値へ戻すので、それより先に合わせても消される。
		// アクタの BeginPlay が呼ばれる順は決まっていない（AWorldSettings::NotifyBeginPlay のアクタの巡回順）ので、
		// まだ呼ばれていなければ合わせずに再試行へ回す
		if (!OrbitPawn->HasActorBegunPlay())
		{
			return false;
		}

		// 注視点は立方体の中心（= パズルのアクタの原点）。距離は散らした全ボクセルが収まるところまで引く
		OrbitPawn->SetOrbitTarget(PuzzleActor->GetActorLocation());
		OrbitPawn->FrameSphere(Radius);
		return true;
	}

	return false;
}

void ACubelithGameMode::PlaySnapSound()
{
	if (SnapSound == nullptr)
	{
		// 人がまだ音を割り当てていない（`.uasset` は人が作る。Docs/SPEC_UE.md 0 章）。
		// 吸着ごとに来るので警告は出さない（ログが埋まる）
		return;
	}

	// 解釈: 2D（定位なし）で鳴らす。モバイルの縦持ちでピースは常に画面内にあり、
	// 音の来る向きを付ける意味が薄いため
	UGameplayStatics::PlaySound2D(this, SnapSound);
}

void ACubelithGameMode::HandleSolvedChanged(bool bInSolved)
{
	// 偽 → 真の変わり目だけを拾う ＝ クリア画面を出すのは一度だけ（配置が変わるたびに来るので、
	// ここで弾かないと同じクリアで何度も画面を作り直してしまう。main.ts の finished と同じ役目）
	if (bInSolved == bSolved)
	{
		return;
	}
	bSolved = bInSolved;

	if (!bSolved)
	{
		UE_LOG(LogCubelith, Log, TEXT("クリア状態ではなくなった"));
		return;
	}

	UE_LOG(LogCubelith, Log,
		TEXT("クリア: 全ピースが N×N×N のどこかに重なりなく収まった（RULES.md 3.4）"));

	ShowClear();
}

void ACubelithGameMode::ShowClear()
{
	// ピースの操作を止める（RULES.md 5.2-4）。走っている回転モードのねじれとスナップの補間は
	// この中で片付き、選択も外れる（クリアした形が歪んだまま・ずれたまま残らないように）。
	// 解釈: 止めるのはピースの操作だけで、カメラの旋回とズームは残す（自動旋回と演出は U5。
	// 詳しくは ACubelithPlayerController::SetPieceInputEnabled の宣言）
	if (ACubelithPlayerController* const PlayerController = GetCubelithPlayerController())
	{
		PlayerController->SetPieceInputEnabled(false);
	}

	// プレイ中 HUD を外してクリア画面へ（BeginScreen が前の画面を必ず外す）
	UCubelithScreenWidget* const Widget = BeginScreen(ECubelithScreen::Clear);
	if (Widget == nullptr)
	{
		return;
	}

	UCubelithClearWidget* const Clear = Cast<UCubelithClearWidget>(Widget);
	if (Clear == nullptr)
	{
		// 人が ClearWidgetClass を別系統のウィジェットに差し替えた。ボタンを結べないので出さない
		UE_LOG(LogCubelith, Error,
			TEXT("ClearWidgetClass（%s）が UCubelithClearWidget の派生ではないのでクリア画面を出せない"),
			*Widget->GetClass()->GetName());
		ActiveWidget = nullptr;
		return;
	}

	// 遊んだ盤面の条件を出す（セッションはまだ畳んでいないので今の値がそのまま使える）
	Clear->SetBoardSummary(GetSessionSpaceSize(), GetSessionPieceCount(), bSessionAllowRotation, GetSessionSeed());
	// 「もう一度」は難易度そのままシードだけ引き直し、「難易度を変える」はタイトルへ戻る（RULES.md 2 章 5）。
	// どちらもセッションを畳んでから作り直す（前の盤面のピース・鍵アイコン・スナップは EndSession で消える）
	Clear->OnRetry.BindUObject(this, &ACubelithGameMode::RestartWithNewSeed);
	Clear->OnBackToTitle.BindUObject(this, &ACubelithGameMode::ReturnToTitle);
	Clear->AddToViewport();
}

void ACubelithGameMode::RetryFrameCamera()
{
	++FrameCameraAttempts;

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (TryFrameCamera())
	{
		World->GetTimerManager().ClearTimer(FrameCameraTimer);
		return;
	}

	if (FrameCameraAttempts >= MaxFrameCameraAttempts)
	{
		World->GetTimerManager().ClearTimer(FrameCameraTimer);
		UE_LOG(LogCubelith, Warning,
			TEXT("軌道カメラ（ACubelithOrbitPawn）が %d 回試しても見つからないのでカメラ合わせを諦めた"),
			MaxFrameCameraAttempts);
	}
}
