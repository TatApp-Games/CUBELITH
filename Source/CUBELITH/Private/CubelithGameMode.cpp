#include "CubelithGameMode.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

#include "CubelithDifficulty.h"
#include "CubelithLog.h"
#include "CubelithOrbitPawn.h"
#include "CubelithPlayerController.h"
#include "CubelithPuzzleActor.h"
#include "CubelithSeed.h"
#include "Generate.h"

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

	/**
	 * クリアの仮表示（AddOnScreenDebugMessage）のキー。同じキーで出し直すと置き換わるので、
	 * 毎フレーム出しても行が増えない。他の表示とぶつからないよう適当に大きな値にしてある
	 */
	constexpr uint64 SolvedMessageKey = 0x4355'4245'4C49'5448ull;

	/**
	 * 仮表示の寿命（秒）。Tick で毎フレーム出し直すので実際はこの秒数まで持たないが、
	 * フレーム落ちで点滅しないよう 1 フレームより十分長くしてある
	 */
	constexpr float SolvedMessageSeconds = 2.0f;

	/** 仮表示の文言。本実装の UI は U4、演出は U5（Docs/SPEC_UE.md 8 章） */
	const TCHAR* SolvedMessageText = TEXT("クリア！ 全ピースが立方体に収まった");
}

ACubelithGameMode::ACubelithGameMode()
{
	// Tick はクリアの仮表示を出し続けるためだけに使う。クリアするまでは回さない（UpdateSolvedDisplay が入れる）
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// マップ /Game/Maps/Main に PlayerStart が無ければ Pawn はワールド原点に湧く。軌道カメラの注視点は
	// 立方体の中心（= パズルのアクタを置くワールド原点）なのでそれでよく、マップには手を入れない
	DefaultPawnClass = ACubelithOrbitPawn::StaticClass();

	// クリック / タップでピースを選ぶコントローラ（RULES.md 3.3）。Blueprint の .uasset を作らないので
	// ここで C++ のクラスを指す（DefaultPawnClass と同じ書き方。Docs/SPEC_UE.md 0 章）
	PlayerControllerClass = ACubelithPlayerController::StaticClass();
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

	StartPuzzle();
}

void ACubelithGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bSolvedShown || GEngine == nullptr)
	{
		return;
	}

	// 同じキーで出し直すと置き換わるので、毎フレーム出せばクリアしている間ずっと見えたままになる
	// （1 フレームだけ出て消えると人が確認できない）
	GEngine->AddOnScreenDebugMessage(SolvedMessageKey, SolvedMessageSeconds, FColor::Green, SolvedMessageText);
}

void ACubelithGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FrameCameraTimer);
	}
	// 終わったあとも画面に残らないよう仮表示を消す（状態の遷移ではないのでログは出さない）
	bSolvedShown = false;
	if (GEngine != nullptr)
	{
		GEngine->RemoveOnScreenDebugMessage(SolvedMessageKey);
	}
	// FGame は UObject ではないのでここで畳む。OnChange は弱参照越しなので、
	// 畳む前に呼ばれても消えた GameMode / アクタには触らない
	Game.Reset();

	Super::EndPlay(EndPlayReason);
}

void ACubelithGameMode::StartPuzzle()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 難易度（Docs/SPEC_UE.md 7.7）。範囲外の丸めは Cubelith::ResolveDifficulty が済ませているので、
	// ここで二重に丸めない（N は 3..7、M は RULES.md 3.1 のプリセットのどれかになっている）
	const Cubelith::FDifficultyResolution Difficulty = ResolveDifficulty();
	const int32 N = Difficulty.SpaceSize;
	const int32 M = Difficulty.PieceCount;
	// 回転操作の可否をコントローラから読めるようにする（ACubelithPlayerController::IsRotationAllowed）
	bResolvedAllowRotation = Difficulty.bAllowRotation;

	const uint32 ResolvedSeed = ResolveSeed();

	// 生成（RULES.md 3.2）
	const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(N, M, ResolvedSeed);

	// 初期散らし（RULES.md 3.2-5）。ヒントで固定したピースを残す Keep は U4 で使うのでここでは空
	Cubelith::FScatterOptions ScatterOptions;
	ScatterOptions.bAllowRotation = Difficulty.bAllowRotation;
	const TArray<Cubelith::FPlacement> Scattered =
		Cubelith::ScatterPlacements(Puzzle.Pieces, N, ResolvedSeed, ScatterOptions);

	// 配置が変わったら描画に反映する（U3 で操作が入ったときに追従する。Game.h の設計どおり）。
	// FGame の寿命は GameMode のメンバとして持つが、コールバックが GameMode より長生きしても
	// 壊れないよう弱参照で握る
	TWeakObjectPtr<ACubelithGameMode> WeakThis(this);
	Game = MakeUnique<Cubelith::FGame>(Puzzle.Pieces, N, Scattered,
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
			// クリア判定は FGame が更新のたびに走らせている（RULES.md 3.4）。ここは結果を見せるだけ
			Self->UpdateSolvedDisplay(bSolved);
			// 操作側（ACubelithPlayerController）へ中継する。スナップ候補の発光を計算し直すきっかけ
			// （RULES.md 5.1。毎フレームは回さず、配置が変わったこのタイミングだけで見る）
			Self->OnPlacementsChanged.Broadcast(Placements);
		});

	// ピースを描くアクタ。原点に置くので、アクタの原点 = 解答空間の中心 = 軌道カメラの注視点になる
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PuzzleActor = World->SpawnActor<ACubelithPuzzleActor>(
		ACubelithPuzzleActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (PuzzleActor == nullptr)
	{
		UE_LOG(LogCubelith, Error, TEXT("ACubelithPuzzleActor を湧かせられなかった"));
		return;
	}

	PuzzleActor->Build(Game->Pieces(), N);
	PuzzleActor->UpdatePlacements(Game->Placements());

	UE_LOG(LogCubelith, Log, TEXT("パズルを開始した: N=%d, M=%d, パズルの回転=%s, seed=%u, ピース数=%d"),
		N, M, Difficulty.bAllowRotation ? TEXT("あり") : TEXT("なし"), ResolvedSeed, Puzzle.Pieces.Num());

	// FGame は構築時にクリア判定を 1 回走らせるが OnChange は呼ばない。散らした直後は普通クリアではないものの、
	// 初期状態も同じ経路に通しておく（U4 で散らし直すときに前のクリア表示が残らないようにするため）
	UpdateSolvedDisplay(Game->Solved());

	// Pawn の生成順に依存するので、まだ湧いていなければ湧くまで短い間隔で試し直す
	if (!TryFrameCamera())
	{
		FrameCameraAttempts = 0;
		World->GetTimerManager().SetTimer(FrameCameraTimer, this, &ACubelithGameMode::RetryFrameCamera,
			FrameCameraRetryIntervalSeconds, /*bLoop=*/true);
	}
}

uint32 ACubelithGameMode::ResolveSeed()
{
	// コマンドライン引数 `-CubelithSeed=<0..4294967295>`。値が付いていなければ空文字のまま（= 未指定）
	const FString CommandLineText = ReadCommandLineValue(SeedCommandLineKey);

	// どれも指定が無いときのために引いておく。起動ごとに変える必要があるが、FMath::Rand は
	// プラットフォームによって 15 bit しか返さないので、時刻で種を撒いた FRandomStream から 32 bit まるごと取る
	const FRandomStream Stream(static_cast<int32>(FDateTime::UtcNow().GetTicks() & static_cast<int64>(MAX_int32)));
	const uint32 RandomSeed = Stream.GetUnsignedInt();

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
	const Cubelith::FDifficultyResolution Resolution = Cubelith::ResolveDifficulty(
		SpaceSizeOptionText, PieceCountOptionText, RotationOptionText,
		ReadCommandLineValue(SpaceSizeCommandLineKey),
		ReadCommandLineValue(PieceCountCommandLineKey),
		ReadCommandLineValue(RotationCommandLineKey),
		SpaceSize, PieceCount, bAllowRotation);

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

void ACubelithGameMode::UpdateSolvedDisplay(bool bSolved)
{
	if (bSolved == bSolvedShown)
	{
		return;
	}
	bSolvedShown = bSolved;

	// 仮表示を出し続けるための Tick は、クリアしている間だけ回す
	SetActorTickEnabled(bSolved);

	if (bSolved)
	{
		UE_LOG(LogCubelith, Log,
			TEXT("クリア: 全ピースが N×N×N のどこかに重なりなく収まった（RULES.md 3.4）"));
		// 次の Tick を待たずに出す（判定の直後に見えるように）
		if (GEngine != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(SolvedMessageKey, SolvedMessageSeconds, FColor::Green, SolvedMessageText);
		}
		return;
	}

	UE_LOG(LogCubelith, Log, TEXT("クリア状態ではなくなった"));
	if (GEngine != nullptr)
	{
		GEngine->RemoveOnScreenDebugMessage(SolvedMessageKey);
	}
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
