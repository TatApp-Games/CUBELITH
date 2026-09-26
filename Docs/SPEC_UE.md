# CUBELITH — UE 版 実装仕様

ステータス: U0 の着手前に決めることを反映（2026-09-26）。残る「未定」は 4 章（対象端末）と 10 章。

- **ゲームルールの正は `RULES.md`**（Web 版・UE 版で共通）。この文書は「それを UE5 でどう実装するか」の正
- 章番号は RULES.md と共通の番号体系。この文書は 0・4・7〜10 章を持ち、1・2・3・5・6 章（ルール）は RULES.md にある
- 最初の仕様書（原典）は `ORIGIN.md`、Web 版の実装仕様は `../WebMock/SPEC.md`

**RULES.md の反映済み: なし**。初回の移植（U1〜U5）が終わった時点の R 番号をここに書き、以後はそこからの差分（RULES.md の変更履歴）を移植する。

## 0. 目的と位置づけ

- 製品本体。iOS / Android 向けの UE5 モバイルアプリ
- Web 版で確定したルールと手触りを移植する。ルールの正は RULES.md、WebMock のコードは参照実装として読む
- 同じ条件とシードなら Web 版と同じパズルを出す（RULES.md 3.6）
- ゲームロジックは C++ で書き、Auto_Tasks（AI）が実装する。マテリアル・Niagara・UMG のレイアウト・MetaSounds などの `.uasset` はバイナリで AI が差分を扱えないので人が作る。人が調整する値は C++ の `UPROPERTY` で公開し、Blueprint はアセットの割り当てに留める
- 人は質感と手触りの仕上げに集中する（原典 1 章の開発方針）

| 項目 | 内容 |
|---|---|
| プラットフォーム | iOS / Android（UE5 モバイルレンダラー）。開発中の実機確認は Android から（iOS のビルドには Mac が要る） |
| エンジン | UE 5.8（Launcher 版。2026-09 時点で 5.8.3）。`.uproject` の `EngineAssociation` は `"5.8"` |
| プロジェクト名 | CUBELITH（`CUBELITH.uproject`） |
| パッケージ名 | `com.tatapp.cubelith`（Android の package 名と iOS の Bundle ID で共通。ストアに公開した後は変えられない） |
| 画面の向き | 縦固定（Portrait） |
| Android | 最低 API 26（Android 8.0。UE 5.8 が対応する下限）。64 bit Arm のみ。Target SDK はエンジンの既定（UE 5.8.3 は 36）に合わせ、ストアに出す前に Google Play の要件を確かめる。パッケージ名・向きは `Config/DefaultEngine.ini` に設定済み |

## 4. 描画と演出の実装（UE への写像）

| ルール / WebMock の該当箇所 | UE 版の実装手段（案） | 担当 |
|---|---|---|
| ピースの描画（`src/render/pieces.ts`） | 1 ピース = 1 `UInstancedStaticMeshComponent`（原典 4.1）。ボクセルの位置は 7.2 の変換で UE の座標へ直す。U2 で `ACubelithPuzzleActor` として実装（ワールド原点 = 解答空間の中心）。仮のメッシュ / マテリアルはエンジンの `/Engine/BasicShapes/Cube` と `BasicShapeMaterial` で、ピースごとに `UMaterialInstanceDynamic` の色を変えている | AI |
| すりガラス（原典 4.2） | 半透明マテリアル（Roughness 高め）+ Fake 屈折 | 人 |
| 内部発光コア（`src/render/glowCores.ts`） | Emissive をサイン波で明滅させるマテリアル。ピースごとの位相はインスタンスごとの値（Per-Instance Custom Data）で渡す | 人（値の受け渡しは AI） |
| 選択・スナップ候補の発光（RULES.md 3.3 / 5.1） | マテリアルのパラメータ。U4 までは**仮の色の持ち上げ**で、`ACubelithPuzzleActor` がピースごとの `UMaterialInstanceDynamic` の `ColorParameterName`（既定 `Color`）へ流す値を変える。選択は `SetSelectedPiece`（白へ寄せてから明るくする。`SelectionWhitenAmount` / `SelectionBrightnessScale`）、スナップ候補は `SetSnapHint`（明るくするだけ。`SnapHintBrightnessScale`。既定 1.25）で、**重なったときは選択が優先**（選択中のピースは白へ寄って既に目立っているため）。強さはすべて `UPROPERTY(EditAnywhere)` なので人がエディタで調整できる。Emissive での本実装は U5 | 人と AI |
| 固定の鍵アイコン（RULES.md 6 章） | 固定中のピースの**各ボクセルの中心**に小さな形を 1 個ずつ出す。U4 までは**仮の見せ方**で、`ACubelithPuzzleActor::SetLockIcon`（`Cubelith::FGame::LockKindOf` の戻り値をそのまま渡せる）が固定の種類ごとの `UInstancedStaticMeshComponent`（銀 = 手動 / 金 = ヒント）のインスタンスを置き直す。ドローコールは**種類ごとに 1 つ = 最大 2 つ**で、その種類の固定が無ければインスタンス 0 個 ＝ 描かれない。既定のメッシュはエンジンの `/Engine/BasicShapes/Sphere`（**本物の南京錠のメッシュ / アイコンは人が後で入れる**）。差し替え口は下の「固定の鍵アイコン」節 | 人と AI |
| 軌道カメラ（`src/render/camera.ts`） | 注視点まわりの軌道カメラを C++ で。U2 で `ACubelithOrbitPawn`（`USpringArmComponent` + `UCameraComponent`）として実装し、`ACubelithGameMode` が開始時にパズルへ合わせる | AI |
| 入力（`src/input/`） | ライントレースでピースを選ぶ。純粋関数（`axisMapping` / `twoFingerGesture` など）はテストごと C++ へ移す。U2 のカメラ操作は、`InputMappingContext` / `InputAction` が `.uasset`（0 章）なので Enhanced Input を使わず、Tick で `APlayerController` から入力状態をポーリングして読む。人がアセットを作る段になれば Enhanced Input へ移せる。U3 で `ACubelithPlayerController` として実装（`PlayerTick` でのポーリング入力・押した瞬間のライントレースでのピックと選択・ドラッグでのグリッド移動・2 本指の 90 度回転）。`src/input` の純粋関数は `CubelithPickSamples` / `CubelithAxisMapping` / `CubelithTwoFingerGesture` / `CubelithFreeRotation` / `CubelithRotateInput`（`Source/CUBELITH/Public`）へ移した。回転中の 90 度に縛らない見せ方は `ACubelithPuzzleActor::SetFreeRotation`。**回転は HUD の回転モードのトグル**（RULES.md 6 章）で出入りし、オンの間は選択中のピースへのドラッグ（マウスの左ボタンでも指でも同じ）が自由回転になって、離した時点で最寄りの向きへ確定する（`SetRotateMode` / `ToggleRotateMode` / `ExitRotateMode`。U3 の「マウスの右ボタンのドラッグ」という仮の手段は U4 でこれに置き換え、右ボタンには何も紐づけていない）。**回転ギズモは作らない**（下の「プレイ中 HUD」節の「解釈:」）。U4 で手を離したときのマグネット・スナップ（`HandlePointerReleased` → `Cubelith::FSnapControl` / `Cubelith::FSnapMotion`）を足した（下の「スナップ」節） | AI（感度の調整は人） |
| スナップの効果音（RULES.md 3.5） | MetaSounds（音そのものは人が作る）。鳴らす口は AI 側にあり、`ACubelithGameMode::SnapSound`（`UPROPERTY(EditAnywhere, Category = "Cubelith|Audio")` の `TObjectPtr<USoundBase>`）に割り当てると `ACubelithGameMode::PlaySnapSound` が `UGameplayStatics::PlaySound2D` で鳴らす。**割り当てが無ければ鳴らない**（警告も出さない）。差し替え方は下の「スナップ」節 | 人と AI |
| クリア演出（RULES.md 5.2） | 発光は Material Parameter Collection、パーティクルは Niagara（原典 5.2）、カメラの旋回は C++ | 人と AI |
| UI（RULES.md 6 章） | UMG。**振る舞いは C++ の基底クラス**（`UCubelithScreenWidget` と `BindWidgetOptional`）、**レイアウトは人が作る**。人のレイアウトが無い間は C++ だけで組んだ仮の画面を出す（下の「画面（UI）」節）。縦持ちの画面に合わせ、ボタンは 44 px 以上。U4 でタイトル / 難易度選択と画面の切り替え・セッションの作り直し・プレイ中 HUD（残りピース数・回転モードのトグル・「固定 / 固定解除」・「散らし直す」・「ヒント」・「次の問題」・「難易度へ戻る」。出す / 押せるの条件は RULES.md 6 章のとおりで、判断は `Cubelith::ResolveHudDisplay`）が入った。HUD は**画面下部**に寄せる（`ConstructBottomPanelRoot`）。残るのはクリア画面 | 人と AI |
| セーブ（RULES.md 3.8） | `USaveGame` を `UGameplayStatics::SaveGameToSlot` で保存する。保存先・保存する形・壊れたデータの扱いは下の「セーブ」節。盤面が変わるたびと、アプリがバックグラウンドに入るとき（`FCoreDelegates` のアプリのライフサイクルの通知）に書く。モバイルでは裏に回ったアプリが OS に終了させられることがあるため | AI |
| ライティング（原典 4.1） | ディレクショナルライト 1 灯 + HDRI | 人 |

- 目標フレームレート: 実機で 30 fps 以上（N=7 / M=27 でも）。対象端末は未定
- **早めに実機で確かめること**（U1 と並行して人が進める）: すりガラスの見た目と負荷、半透明の ISM でインスタンス同士の前後関係が崩れないか（インスタンス単位では並び替えられない）、Fake 屈折がモバイルで成り立つか

### スナップ

RULES.md 3.5（手を離したときのマグネット・スナップ）と 5.1（候補の発光）の実装。候補を求める純粋関数
`Cubelith::SnapCandidate` は `Source/CUBELITHCore/Public/Solve.h`（U1 で移植済み）で、ここはその「いつ呼ぶか」と
結果の配り先。移植元は Web 版の `WebMock/src/input/snapControl.ts`・`WebMock/src/render/snapMotion.ts` と、
それらを繋いでいる `WebMock/src/main.ts`。

| 役目 | 実装 |
|---|---|
| 候補の有無と吸着先を決める | `Cubelith::FSnapControl`（`Source/CUBELITH/Public/CubelithSnapControl.h`）。`Refresh` は光らせる対象が変わったときだけ `true`、`Release` は吸着先があれば「吸着前 / 吸着後の配置」を返す。**現在位置がそのまま候補になる（＝すでに収まっている）ときは吸着先として扱わない** |
| 見た目を追いつかせる | `Cubelith::FSnapMotion`（`CubelithSnapMotion.h`）。論理上の配置は離した瞬間に整数座標で確定させ、表示だけを `easeOutCubic` で 0 へ戻す。既定は **130 ms**（`Cubelith::SnapDurationSeconds`。RULES.md 3.5 の 100〜150 ms の中央付近で、TS の `SNAP_DURATION_MS` と同じ）。人が変えるのは `ACubelithPlayerController::SnapDurationSeconds`（秒） |
| 表示だけのずれ | `ACubelithPuzzleActor::SetViewOffset` / `ClearViewOffset`（グリッド単位。`pieces.ts` の `setOffset`）。ロジックの配置は動かないので、補間中もクリア判定と残りピース数は整数座標のまま |
| 候補の発光 | `ACubelithPuzzleActor::SetSnapHint`（上の 4 章の表のとおり仮の色の持ち上げ。本実装は U5） |
| 入力への接続 | `ACubelithPlayerController::HandlePointerReleased`（指 / 左ボタンを離した時点）。配置が変わったとき・選択が変わったとき・回転が確定したときに `Refresh` を呼び（毎フレームは回さない）、手でドラッグし直した / 回したときは走っている補間を打ち切る。配置の変化は `ACubelithGameMode::OnPlacementsChanged`（`Cubelith::FGame` の `OnChange` からの中継）で受ける |

**効果音と発光の差し替え口**

- **効果音**: `ACubelithGameMode` の `UPROPERTY(EditAnywhere, Category = "Cubelith|Audio")` の `TObjectPtr<USoundBase> SnapSound`。`ACubelithPlayerController` が吸着を確定させた時点で `ACubelithGameMode::PlaySnapSound` を呼び、`UGameplayStatics::PlaySound2D` で鳴らす。**割り当てが無ければ鳴らさない**（吸着ごとに来るので警告も出さない）
  - 人の手順: `ACubelithGameMode` の Blueprint 派生を作って `SnapSound` に MetaSound（または `USoundWave`）を割り当て、`Config/DefaultEngine.ini` の `GlobalDefaultGameMode` をその Blueprint に差し替える。`.uasset` は AI が作らないので（0 章）、置き場所を「人がエディタで割り当てられるところ」にしてある
  - 解釈: 2D（定位なし）で鳴らす。モバイルの縦持ちでピースは常に画面内にあり、音の来る向きを付ける意味が薄いため
- **発光**: `ACubelithPuzzleActor` の `SnapHintBrightnessScale`（候補）と `SelectionWhitenAmount` / `SelectionBrightnessScale`（選択）。どれも `UPROPERTY(EditAnywhere, Category = "Cubelith|Render")` で、`VoxelMaterial` を差し替えても `ColorParameterName` のベクトルパラメータがあれば効く。U5 でマテリアルの Emissive に移すときは、この 3 つを新しいパラメータ名へ読み替える
- 補間の時間は `ACubelithPlayerController` の `SnapDurationSeconds`（`UPROPERTY(EditAnywhere, Category = "Cubelith|Input")`、秒。既定 0.130）

Automation Test は `CUBELITH.Render.Snap.*`（`FSnapControl`）と `CUBELITH.Render.SnapMotion.*`（`FSnapMotion`）。
どちらも `UWorld` を作らずに純粋な状態機械として確かめる（時刻は呼び出し側から渡す形にしてあるので、実時間を待たない）。

### 固定の鍵アイコン

RULES.md 3.3（固定・やり直し）・3.7（ヒント）・6 章（固定の表示）の実装。本体は U1 で `CUBELITHCore` に移植済みで、ここはその「いつ呼ぶか」と見せ方。移植元は `WebMock/src/main.ts` の `onToggleLock` / `onHint` / `onReset` と `WebMock/src/render/lockIcons.ts`。

**操作（`ACubelithGameMode`）** — プレイ中 HUD（下の「プレイ中 HUD」節）とセーブからの復元はここを呼ぶ

| 操作 | 実装 | 使う `CUBELITHCore` の関数 |
|---|---|---|
| 固定 / 固定解除 | `ToggleLock(PieceId)`。未固定なら手動の固定を付け、手動の固定中なら外す。**ヒントの固定は解除できない**（RULES.md 3.3）。固定する前に回転モードを抜けて走っている自由回転を確定させ（`ACubelithPlayerController::ExitRotateMode`）、固定した位置で止めるためスナップの補間を打ち切る（`CancelSnapMotionFor`） | `Cubelith::FGame::Lock` / `Unlock` / `LockKindOf` |
| ヒント | `UseHint()`。対象を選び、**解答の位置と向きへ置いてから固定する**（`Place` は固定済みに効かないのでこの順が要る）。使えるかの問い合わせは `IsHintAvailable()`（HUD がボタンの有効 / 無効に使う） | `Cubelith::PickHintPiece`（`Hint.h`）・`FGame::Place` / `Lock` / `LockedIds` |
| 散らし直し | `ScatterAgain()`。ヒントで固定したピースの現在の配置を `Keep` に入れ、**同じシード**（`GetSessionSeed`）で散らし直す。回転モードを抜け（`ExitRotateMode`）、選択を外し（`ACubelithPlayerController::SetSelectedPiece(INDEX_NONE)`）、スナップの補間はすべて打ち切る（`CancelAllSnapMotion`） | `Cubelith::ScatterPlacements` の `FScatterOptions::Keep`（`Generate.h`）・`FGame::Reset`（手動の固定を解き、ヒントの固定は残す） |
| 表示の揃え直し | `RefreshLockIcons()`。**固定 / 固定解除は配置を変えない ＝ `FGame` の `OnChange` が来ない**ので、固定の状態を変えた操作が自分で呼ぶ。全ピースに今の種類を流すので、一度に複数変わる経路（散らし直し・セーブからの復元）でもそのまま使える | `FGame::LockKindOf` |

- 繋ぐときに要る小さな判断は `Source/CUBELITH/Public/CubelithLockOps.h` に純粋関数として切り出してある（`DecideLockToggle` / `IsHintAvailable` / `CollectHintKeptPlacements` / `LockIconColor`）。Automation Test は `CUBELITH.Render.LockOps.*`
- 解釈: 散らし直しで `Keep` に入れるのは「ヒントの固定が付いているピースの**現在の**配置」（ヒントは解答位置へ置くので解答配置と同じ値になるが、Web 版の `main.ts` と同じく現在の配置から作る）
- 固定中のピースは選べるが動かせない（`ACubelithPlayerController::IsPieceLocked` が移動・回転・スナップを弾く）。固定した瞬間にスナップ候補の発光が消え、解除した瞬間に計算し直されるのは、操作のあとに `ACubelithPlayerController::RefreshSnapHint` を呼んでいるため

**仮の見せ方と人が差し替える口**（`ACubelithPuzzleActor`。すべて `UPROPERTY(EditAnywhere, Category = "Cubelith|Lock")`）

| `UPROPERTY` | 型 | 既定 | 人が入れるもの |
|---|---|---|---|
| `LockIconMesh` | `TObjectPtr<UStaticMesh>` | `/Engine/BasicShapes/Sphere` | **本物の南京錠のメッシュ、または板ポリのアイコン**。空ならアイコンを出さない（警告を 1 回出す） |
| `LockIconMaterial` | `TObjectPtr<UMaterialInterface>` | `/Engine/BasicShapes/BasicShapeMaterial` | 金属の質感のマテリアル（U5） |
| `LockIconColorParameterName` | `FName` | `Color` | 差し替えたマテリアルの色パラメータ名 |
| `LockIconSizeRatio` | `float` | `0.5`（半マス。`lockIcons.ts` の `ICON_SIZE` と同じ） | 見え方の調整 |
| `ManualLockIconColor` | `FLinearColor` | 銀（`0.85, 0.88, 0.94`） | 手動の固定の色 |
| `HintLockIconColor` | `FLinearColor` | 金（`1.0, 0.78, 0.12`） | ヒントの固定の色 |

- 人の手順: 既定の `ACubelithPuzzleActor` は `ACubelithGameMode` が C++ のクラスから湧かせているので、**このクラスの Blueprint 派生を作って上の値を入れ、`ACubelithGameMode` が湧かせるクラスをそれに差し替える**（`SnapSound` と同じ「人がエディタで割り当てられる場所」。`.uasset` は AI が作らない。0 章）。差し替えの口を足すのは人がアセットを用意した時点でよい
- 解釈: アイコンはボクセルの**中心**に置き、ピースのボクセル（`VoxelFillRatio` = 既定 0.96 でわずかに縮めてある）より小さくして中に見えるようにする。大きさは `LockIconSizeRatio` で人が調整する
- アイコンはライントレースに当たらせない（`ECollisionEnabled::NoCollision`）。当たると固定中のピースを押したときにアイコンがピース本体の手前で遮ってしまう
- 表示だけのずれ（スナップの補間）が掛かっていれば同じだけずらすが、自由回転は見ない（固定中のピースは回せず、固定の直前に走っていた分は `ToggleLock` が確定させてから固定する）
- **RULES.md 6 章の「そのピースの内部発光コアは消す」はまだ扱っていない**（内部発光コア自体が U5 で人が作るマテリアルなので、消す相手がまだ無い）

### セーブ

RULES.md 3.8 の「覚えている 3 つ」を `USaveGame` で保存する。実装は `Source/CUBELITH/Public/CubelithSave.h`（保存する形と、検証・更新の純粋関数）と `CubelithSaveGame.h`（器とスロットへの読み書き）。移植元は Web 版の `WebMock/src/ui/save.ts` で、JSON ではなく `USaveGame` のシリアライズを使うので、TS の `parseSaveData` / `serializeSaveData` は「読み込んだ値が使える形か検証する」関数（`IsValidSavedDifficulty` / `IsValidSavedProgress` / `SanitizeSaveData`）として移した。

**保存先**

- スロット名 `CubelithSave`・ユーザー index `0`（`Cubelith::SaveSlotName` / `SaveUserIndex`）。実ファイルは `Saved/SaveGames/CubelithSave.sav`
- 版番号 `Cubelith::SaveVersion`（現在 1）を `UCubelithSaveGame::Version` に持つ（Web 版の `localStorage` のキー `cubelith.save.v1` に当たる）。保存する形を変えたら 1 つ上げる

**保存する形**（`FCubelithSaveData`。すべて `USTRUCT` / `UENUM` で、`UCubelithSaveGame` はこれを 1 つ持つ）

| 覚えるもの | 形 |
|---|---|
| 最後に選んだ難易度 | `FCubelithSavedDifficulty`（`SpaceSize` / `PieceCount` / `bAllowRotation`） |
| 途中の盤面 | 有無のフラグ `bHasProgress` + `FCubelithSavedProgress`（難易度・`Seed`・`Placements`・`Locks`・`Remaining`） |
| クリア回数 | `FCubelithSavedClears`（`Total` と `ByDifficulty`。キーは `Cubelith::DifficultyKey` の `"3-4-0"` 形式 = `N-M-回転`） |

- ピースの形は保存しない。難易度と `Seed` から `GeneratePuzzle` で再現できる（RULES.md 3.6）ので保存量が小さく、生成規則が変わったときにはピース数の食い違いとして検出できる
- `Seed` は `int64` で持つ（`uint32` は `UPROPERTY` にできないため。`ACubelithGameMode::Seed` と同じ扱い）。負の値は「保存されていない」
- `Locks` は固定中のピースだけを id 昇順で持つ。種類は `ECubelithSavedLockKind`（`Manual` / `Hint`）で、`Cubelith::ELockKind`（`CUBELITHCore` は `UObject` を持てない。7.1）との相互変換は `ToCoreLockKind` / `ToSavedLockKind`
- `Remaining` はタイトルに出す残りピース数（RULES.md 6 章）。盤面を再生成せずに出せるよう保存する。**数え方は `CubelithProgress.h` の「解釈:」**（面接触の塊 → 最大の塊 → 外接ボックスが N×N×N に収まれば確定）。移植元は `WebMock/src/ui/progress.ts`
- 選択中のピースとカメラは保存しない（RULES.md 3.8。再開時は「選択なし・カメラは初期位置」）

**壊れたデータの扱い**（`Cubelith::LoadSaveData` → `SanitizeSaveData`）

読めないものは例外を出さず、既定（RULES.md 3.1 の N=3 / M=4 / 回転なし、途中の盤面なし、クリア回数 0）へ落とす。

- スロットが無い / 読めない / 別のクラス / 版番号が `SaveVersion` と違う → まるごと既定値
- 最後に選んだ難易度が範囲外（N が 3..7 の外、M が 2..`MaxPieces(N)` の外）→ まるごと既定値
- 途中の盤面が使えない形 → **盤面だけ捨てて難易度とクリア回数は生かす**（Web 版の `parseSaveData` と同じ）。弾くのは、難易度が範囲外・`Seed` が 0..4294967295 の外・配置が空・配置数が難易度の M と食い違う・向きが 0..23 の外・`PieceId` が重複・固定の id が配置に無い / 重複・`Remaining` が 0..配置数の外。復元して初期配置に使う前にはさらに `ProgressFitsPieces` で「生成したピースの id とちょうど 1 対 1 か」を見る
- クリア回数 → 負の合計は 0 に、負の回数のキーは落とす（回数は遊びの進行に影響しないので、全体を捨てるより残すほうが損が小さい）

**繋いだところ**: 「最後に選んだ難易度」の読み出し（タイトルの初期選択。下の「画面（UI）」節）。

**まだ繋いでいないこと**: 保存のタイミング（盤面が変わるたび・バックグラウンドに入るとき）と「続きから」・クリア回数の表示は後続タスクで `ACubelithGameMode` から繋ぐ。ここまでは形と純粋関数・スロットへの読み書きの器だけ。Automation Test（`CUBELITH.Render.Progress.*` / `CUBELITH.Render.Save.*`）は実際のスロットへ読み書きしない（エディタの `Saved/` を汚さないため）ので、検証・更新の純粋関数を直接呼んで確かめている

### 画面（UI）

RULES.md 2 章のコアゲームループと 6 章の画面を UMG で作る。**画面のレイアウトの `.uasset`（ウィジェットブループリント）は人が作る**（0 章）ので、このリポジトリに入っているのは「振る舞いを持つ C++ の基底クラス」と「人のレイアウトが無いときだけ使う仮の見た目」。移植元は Web 版の `WebMock/src/ui/`（`screens.ts` / `titleScreen.ts` / `widgets.ts`）。

**仮の画面を C++ だけで作る仕組み**（`Source/CUBELITH/Public/CubelithScreenWidget.h`）

| 役目 | 実装 |
|---|---|
| 画面 1 つ分の基底 | `UCubelithScreenWidget : UUserWidget`。派生クラスが `BuildFallbackLayout`（仮のレイアウトを組む）と `BindBehavior`（部品に振る舞いを付ける）を実装する |
| 人のレイアウトがあるかの判定 | `WidgetTree->RootWidget` の有無。ウィジェットブループリントから作ったウィジェットは必ず根の部品を持ち、C++ のクラスから直接作ったものは必ず持たない（`UUserWidget::Initialize` が空の `UWidgetTree` を作るだけ）。**無いときだけ** `UWidgetTree::ConstructWidget` で C++ からレイアウトを組む。部品を 1 つも置いていないウィジェットブループリントも「無い」側に入る |
| 部品の受け取り | `UPROPERTY(meta = (BindWidgetOptional))`。**`BindWidget` ではなく必ず Optional にする**（埋まっていない部品は触らないだけなので、人のレイアウトに一部が無くても落ちない） |
| 振る舞いの置き場所 | `BindBehavior`。仮の画面でも人のレイアウトでも呼ばれるので、**押したときの処理はここだけに書く**（仮のレイアウトを組むときには結ばない） |
| ボタン 1 個 | `ConstructButton(親, ラベル, 押したときの処理, 最小の横幅)`。指で押せる大きさ（RULES.md 6 章の 44 px 以上 = `Cubelith::MinTouchTargetPx`）を `USizeBox` で確保する。HUD・クリア画面でも同じヘルパを使う |
| 画面のどこに寄せるか | `ConstructCenteredPanelRoot`（中央寄せ。タイトル / クリア）と `ConstructBottomPanelRoot`（**画面下部**に寄せて横幅いっぱい。HUD。RULES.md 6 章の「スマホでは HUD を画面下部に寄せ」）。作りは同じ（Overlay → Border → VerticalBox）で、パネルを置く位置だけが違う |
| ラムダを結ぶ仕組み | `UButton::OnClicked` は動的デリゲートで `UFUNCTION` しか結べないので、`UCubelithButtonAction`（`UFUNCTION` 1 つと `TFunction<void()>` 1 つを持つ小さな `UObject`）を挟む。これで「N = 4 のボタン」のように値ごとに違う処理をラムダで書ける。寿命は画面のウィジェットが握る |
| その他のヘルパ | `ConstructText`・`ConstructRow`・`SetButtonSelected`（選択を色で見せる）・`SetTextSafe`・`SetButtonLabel`（ラベルを入れ替える。HUD の「回転 / 回転解除」「固定 / 固定解除」）・`SetButtonVisible` / `SetWidgetVisible`（出す / 隠す。隠すときは場所も取らない `Collapsed`） |

- 仮の見た目は縦持ちの画面を前提に、**色とフォントの指定は最小限**にする（文字が読める・押せる・選んでいるものが分かるだけ。人が UMG で作り直す前提）。フォントは既定のまま使い（日本語は Slate の代替フォントで出る）、文字の大きさだけ指定する
- 文言は `FText::FromString` で日本語を直接書く（対応する言語が未定なのでローカライズの仕組みは入れない。10 章）

**画面と切り替えの持ち主**

| 画面 | クラス | 状態 |
|---|---|---|
| タイトル / 難易度選択（RULES.md 6 章） | `UCubelithTitleWidget`（`CubelithTitleWidget.h`） | 入っている。N（3〜7）・M のプリセット・パズルの回転・シードの表示・「開始」。**「続きから」とクリア回数の表示は後続タスク** |
| プレイ中の HUD（RULES.md 6 章） | `UCubelithHudWidget`（`CubelithHudWidget.h`） | 入っている。残りピース数・回転モードのトグル・「固定 / 固定解除」・「散らし直す」・「ヒント」・「次の問題」・「難易度へ戻る」（下の「プレイ中 HUD」節） |
| クリア | — | 後続タスク（`ClearWidgetClass` が空） |

- **解釈: ウィジェットの生成・`AddToViewport`・`RemoveFromParent` は `ACubelithGameMode` 1 箇所にまとめる**（`ACubelithPlayerController` ではない）。どの画面を出すかは「セッションが有るか・クリアしたか」= GameMode が持つ状態で決まり、画面の入れ替えとセッションの作り直しを同じ場所で行えると順序の取り違えが起きないため。人が差し替える口（`TSubclassOf`）も効果音（`SnapSound`）と同じ場所に集まる
- 今どの画面かは `ECubelithScreen`（`None` / `Title` / `Play` / `Clear`。`screens.ts` の `ScreenName`）。切り替えは `ACubelithGameMode::BeginScreen` が**前の画面を必ず `RemoveFromParent` してから**作る（`screens.ts` の `show` が前の画面を `dispose` するのと同じ）。クラスが空の画面は「前の画面を外すだけ」になるので、クリア画面（`ClearWidgetClass`）を後から載せられる
- 入力は `ACubelithPlayerController::BeginPlay` で `FInputModeGameAndUI` にしてある（画面のボタンとゲームの操作を同時に効かせる。マウスを掴むのは押している間だけなので、カメラの旋回とピースのドラッグはそのまま通る。カーソルは掴んでいる間も出したまま）

**人が UMG へ差し替える手順**

1. エディタで Widget Blueprint を作り、親クラスに差し替えたい画面の C++ クラス（タイトルなら `CubelithTitleWidget`）を選ぶ
2. その C++ クラスの `BindWidgetOptional` と**同じ名前・代入できる型**で部品を置く。タイトルは `TitleText`・`LeadText`（`UTextBlock`）、`SpaceSizeRow`・`PieceCountRow`・`RotationRow`（`UPanelWidget`。`UHorizontalBox` などでよい）、`SummaryText`・`SeedText`（`UTextBlock`）、`StartButton`（`UButton`）。HUD は下の「プレイ中 HUD」節の表のとおり。名前が合っていれば C++ がそこへ値と処理を流す。要らない部品は置かなくてよい（`SummaryText` が無ければまとめの行が出ないだけ）
3. 選択肢のボタン（N / M / 回転）は**数が N で変わるので C++ が並べる**。人が置くのは入れ物（`SpaceSizeRow` など）だけで、中身は C++ が作って入れ替える
4. `ACubelithGameMode` の Blueprint 派生（効果音の割り当てと同じもの）で `TitleWidgetClass`（HUD なら `PlayWidgetClass`。カテゴリ `Cubelith|UI`）を作ったウィジェットブループリントに差し替える。`Config/DefaultEngine.ini` の `GlobalDefaultGameMode` がその Blueprint を指していること
5. 根の部品が無い（何も置いていない）ウィジェットブループリントを指した場合は、仮のレイアウト（C++）に落ちる。`StartButton` が無いと「開始」を押せない・HUD の `BackToTitleButton` が無いと盤面から出られないので、どちらも `LogCubelith` に警告が出る
6. 差し替えたクラスがその画面の C++ クラス（`UCubelithTitleWidget` / `UCubelithHudWidget`）の派生でなければ、値も処理も結べないので**その画面を出さず** `LogCubelith` にエラーを出す

**セッション（パズル 1 回分）**

`ACubelithGameMode` が RULES.md 2 章のコアゲームループを持つ。難易度（N / M / パズルの回転）+ シードの 1 組を「セッション」として作る / 畳む / 作り直す。

| 口 | すること |
|---|---|
| `ShowTitle(N, M, 回転, シード)` | 走っているセッションを畳んでタイトルを出す（初期選択とシードは呼び出し側が決める） |
| `ReturnToTitle()` | 直前の難易度が選ばれた状態でタイトルへ戻る（シードは引き直す。RULES.md 2 章の「難易度を変える」「難易度へ戻る」） |
| `StartSession(N, M, 回転, シード)` | 生成 → 初期散らし → `Cubelith::FGame` → `ACubelithPuzzleActor` → 軌道カメラの距離合わせ → プレイ中の画面へ。N は 3..7、M は N ごとのプリセットへ寄せる |
| `RestartWithNewSeed()` | 同じ難易度でシードだけ引き直して作り直す（RULES.md 2 章の「もう一度」「次の問題」） |
| `EndSession()` | タイマー・ピースのアクタ・ゲーム状態・クリアの仮表示を畳み、`ACubelithPlayerController::ResetForNewSession` で選択・ドラッグ・スナップも白紙に戻す |
| `DrawRandomSeed()` | 新しいシードを引く（`difficulty.ts` の `randomSeed`。時刻と呼んだ回数で撒いた `FRandomStream` から 32 bit） |

- セッションの間は生成結果（`Cubelith::FGeneratedPuzzle` の `Pieces` と `Solution`・N / M / シード）とパズルの回転をメンバに持つ（`GetSessionPuzzle` / `GetSessionSeed` / `GetSessionSpaceSize` / `GetSessionPieceCount` / `IsRotationAllowed` / `HasSession`）。後続タスクのヒントが `Solution`、散らし直しが同じシード、セーブが難易度とシードを読む
- 起動時（`BeginPlay`）はタイトルを出すだけで、**パズルは「開始」を押してから作る**
- 軌道カメラの距離合わせ（`TryFrameCamera`）はセッションを作るたびに掛け直す（N が変われば収める大きさも変わる）
- ピースの形は同じ N / M でもシードで変わるので、作り直すときは `ACubelithPlayerController` のスナップの制御（`Cubelith::FSnapControl`）も必ず作り直す（ピース数の一致では判定しない）

**タイトルの初期選択**（7.7 の外部指定とセーブの関係）

- 優先順位は **外部指定（7.7 の `?n=` / `?m=` / `?rot=` とコマンドライン）> セーブの「最後に選んだ難易度」（RULES.md 3.8）> `ACubelithGameMode` の `UPROPERTY` の既定**（Web 版 `WebMock/src/main.ts` の `initialSettings` と同じ）
- 解釈: セーブの難易度は `Cubelith::ResolveDifficulty` の「`UPROPERTY` の既定」の位置へ差し込む。こうすると解釈の純粋関数とそのテスト（`CUBELITH.Render.Difficulty.*`）を触らずに順位を作れる。セーブが無いときだけ `UPROPERTY` が効くよう、`UGameplayStatics::DoesSaveGameExist` でスロットの有無を先に見る（ログの経路「プロパティ」は、セーブがあればセーブの値のこと。何を渡したかは 1 行前に出る）
- シードは外部指定があればその値、無ければ引き直し（シードは保存しない。RULES.md 3.8）。タイトルへ戻るたび・「もう一度」のたびに引き直す
- **外部指定が効くのは初期選択まで**。「開始」を押したときはタイトルで選ばれている値で始まる（7.7 の口は検証用に残す）

Automation Test は `CUBELITH.Render.TitleWidget.*`（仮のレイアウトが組まれること・初期選択をプリセットへ寄せること・N を変えると M のプリセットが作り直されて選択が引き継がれること・「開始」が選ばれている値を渡すこと）。Slate の実体は作らず、`UUserWidget::Initialize` と `UCubelithScreenWidget::PrepareLayout` で部品の木だけを組み、ボタンは `UButton::OnClicked` を直に鳴らして押す（`UWorld` が要らないので `-nullrhi` のコマンドラインでも走る）。

画面の見え方（色・大きさ・並び）と実機の操作は機械判定できないので、人がエディタで確認する（9 章）。

### プレイ中 HUD

RULES.md 6 章の「プレイ中 HUD」と、RULES.md 3.3 の回転の手段のうち「回転モードのドラッグ」の実装。
画面の作りは上の「画面（UI）」節の仕組みに乗っていて、**振る舞いは `UCubelithHudWidget`**（`Source/CUBELITH/Public/CubelithHudWidget.h`）、
**出す / 押せる / ラベルの判断は純粋関数 `Cubelith::ResolveHudDisplay`**（`CubelithHudState.h`）、
**盤面を触るのは `ACubelithGameMode`** の 3 つに分かれている。移植元は Web 版の `WebMock/src/ui/hud.ts` と、
それを繋いでいる `WebMock/src/main.ts` の HUD のコールバック。

仮のレイアウトは縦持ちの画面を前提に**画面下部**へ寄せ（`ConstructBottomPanelRoot`）、
上から「残りピース数 / 案内の 1 行 / ピースに紐づく操作の行（選択中だけ）/ 常に出す行」の 4 段に並べる。
ボタンはどれも 44 px 以上（`ConstructButton`）。

**部品**（`BindWidgetOptional`。人が UMG で置くときの名前）

| 部品 | 型 | 役目 |
|---|---|---|
| `RemainingText` | `UTextBlock` | 残りピース数（`残り 2 / 5`）。数え方は `CubelithProgress.h` の「解釈:」（`Cubelith::UnsettledPieceCount`） |
| `StatusText` | `UTextBlock` | 今できる操作の案内（未選択 / 固定中 / ヒントの固定 / 回転モード中 / 回転のあり・なし で文が変わる。`hud.ts` の `#hud-hint`） |
| `PieceControlsRow` | `UPanelWidget` | ピースに紐づく操作の入れ物。**選択していない間は行ごと隠す** |
| `RotateModeButton` | `UButton` | 回転モードのトグル（「回転 / 回転解除」） |
| `LockButton` | `UButton` | 「固定 / 固定解除」 |
| `FooterRow` | `UPanelWidget` | 常に出すボタンの入れ物（人のレイアウトでは使わなくてよい） |
| `BackToTitleButton` / `ScatterAgainButton` / `HintButton` / `NextButton` | `UButton` | 「難易度へ戻る」「散らし直す」「ヒント」「次の問題」 |

- ラベルが入れ替わるボタン（`RotateModeButton` / `LockButton`）は、**`UTextBlock` を直の子**に置く（`SetButtonLabel` がそこへ書く）。別の作りにすると文字が変わらないだけで、押すことはできる
- 隠す / 出すは `Collapsed` と部品のクラスの既定値で行う（`SetWidgetVisible`）。仮の画面のボタンは `USizeBox` で包んであるので、隠すときは入れ物ごと隠して 44 px の空白を残さない

**出す / 押せる の条件**（RULES.md 6 章。判断は `Cubelith::ResolveHudDisplay`、材料は `Cubelith::FHudState`）

| 条件 | 決め方 |
|---|---|
| 回転モードのトグルと「固定 / 固定解除」を出す | ピースを選択しているときだけ（`PieceControlsRow` ごと隠す） |
| 回転モードのトグルを出す | パズルの回転「あり」のときだけ（`ACubelithGameMode::IsRotationAllowed`） |
| 回転モードのトグルを押せる | 選択中のピースが固定されていないとき |
| 「固定解除」を押せる | ヒントの固定でないとき（**ヒントの固定は解除できない**。RULES.md 3.3） |
| 「ヒント」を押せる | `ACubelithGameMode::IsHintAvailable`（未固定のピースが 2 個以上。RULES.md 3.7） |

- 解釈: `hud.ts` は回転「なし」のときトグルを**作らない**が、UE 版は人の UMG に置かれているかもしれないので「作ってから隠す」に揃えた（`Collapsed` なので場所も取らず、見え方は同じ）
- 解釈: `hud.ts` は「ヒントの固定は解除できない」をボタンの `title` 属性（吹き出し）で伝えている。UMG の仮の画面に吹き出しは無いので、**ヒントの固定だけ案内の文を分けて**押せない理由が読めるようにした

**ボタンの行き先**（`ACubelithGameMode::BindHud`。押されたことだけを受け取り、盤面を触るのは GameMode）

| ボタン | 行き先 |
|---|---|
| 回転モードのトグル | `ACubelithPlayerController::ToggleRotateMode`（入れたかを決めるのは入力側）→ `RefreshHud` でラベルへ返す |
| 「固定 / 固定解除」 | `ToggleLock(選択中のピース)`（上の「固定の鍵アイコン」節） |
| 「散らし直す」 | `ScatterAgain()` |
| 「ヒント」 | `UseHint()` |
| 「次の問題」 | `RestartWithNewSeed()`（難易度はそのままシードだけ引き直す。RULES.md 2 章） |
| 「難易度へ戻る」 | `ReturnToTitle()`（直前の難易度が選ばれた状態・シードは引き直し。RULES.md 2 章） |

**表示を更新する経路**: `ACubelithGameMode::RefreshHud()` 1 本にまとめてある（Web 版 `main.ts` の
`hud.setSelected` / `setRemaining` / `setLock` / `setRotateMode` / `setHintEnabled` を 1 か所に集めたもの。
ウィジェット側の口はその 5 つのまま残してある）。**差分を追わず**今の状態を丸ごと流し込むので、
呼ぶ側は「何かが変わった」ことだけを知っていればよい。呼ぶのはセッションを作ったとき・配置が変わったとき
（`Cubelith::FGame` の `OnChange`）・選択が変わったとき（`ACubelithPlayerController::SetSelectedPiece`）・
固定の状態が変わったとき（`ToggleLock` / `UseHint` / `ScatterAgain`）・回転モードを切り替えたとき。
HUD を出していない画面（タイトル / クリア）では何もしない。

**回転モード**（`ACubelithPlayerController`。RULES.md 3.3 の「回転モードのドラッグ」）

- オンの間は、選択中のピースを押したドラッグが移動ではなく**自由回転**になる（90 度に縛らずに見せ、離した時点で最寄りの向きへ確定する）。確定は U3 の `CommitRotateDrag`（`Cubelith::SnappedOrientation` → `Cubelith::FGame::Place` → `ACubelithPuzzleActor::ClearFreeRotation`）をそのまま通る。**マウスの左ボタンでも指でも同じように働く**
- 入れる条件は `SetRotateMode`: パズルの回転「あり」・ピースを選んでいる・そのピースが固定されていない。入れたかどうかは戻り値で返し、`RefreshHud` がラベル（「回転 / 回転解除」）へ反映する（`main.ts` の `onToggleRotateMode` と同じ形）
- **抜けるときは必ず表示だけのねじれを解く**（`ExitRotateMode` が `CommitRotateDrag` を通す）。通る経路は、トグルで抜ける・選択が変わる（`SetSelectedPiece`）・固定する（`ToggleLock`）・散らし直す（`ScatterAgain`）・クリアする（`UpdateSolvedDisplay`）・セッションを作り直す（`ResetForNewSession`）
- 固定中のピースを選んでいる間はフラグが立っていてもドラッグは回らない（ドラッグを始めるときに固定を見る）。HUD もそのときラベルを「回転」へ戻す（`hud.ts` の `rotating` と同じ扱い）
- 2 本指（タッチ）の 90 度回転（U3）はそのまま残す。ただし**回転モード中は 90 度回転を行わない**（回転はモード中のドラッグへ一本化する。`pieceInput.ts` の `applyTwoFinger` と同じ）。ピンチのズームはどちらでも効く
- 解釈: RULES.md 3.3 は回転の手段として「2 本指スワイプ / ひねり、回転モードのドラッグ、**または**回転ギズモ」を挙げているが、**回転ギズモは作らない**（U4 の範囲は「マウスで回転を確かめる仮の手段 → HUD の回転モードのトグル」の置き換えまで）。足すかは後の段階で人が決める
- U3 でマウスの右ボタンに紐づけていた自由回転は**外した**（仮の手段だった）。右ボタンには何も紐づいていない

Automation Test は `CUBELITH.Render.HudState.*`（上の「出す / 押せる の条件」とラベル・案内の文・残りピース数の文字）。
**ウィジェットもアクタも立てない**（判断を純粋関数へ切り出してあるので、状態を渡すだけで確かめられる）。
並びや色・実機での押しやすさと回転の手触りは機械判定できないので、人がエディタで確認する（9 章）。

## 7. 技術構成

ビルド・テストのコマンドと開発ルールは `../Source/CLAUDE.md`（UE 版の CLAUDE.md）。

```
CUBELITH.uproject
Config/
Content/                  人が作るアセット
Scripts/                  ビルド・テスト・エディタ起動（pwsh）
Source/
    CLAUDE.md             UE 版の CLAUDE.md
    CUBELITH/             ゲーム本体（描画・入力・UI・セーブの C++）
    CUBELITHCore/         ゲームロジック（RULES.md 3 章）。WebMock の src/core に対応し、描画に依存しない
        Private/Tests/    Automation Test と照合データ（Fixtures/）
Docs/                     RULES.md / SPEC_UE.md / FIXTURES.md / ORIGIN.md
WebMock/                  Web 版（参照実装と照合データの出どころ）
```

### 7.1 モジュール

- `CUBELITHCore` が依存してよいのは `Core` モジュールだけ（`CoreUObject`・`Engine` を使わない。UObject と `UPROPERTY` を持たない）。例外は照合データを読むための `Json`（テストのコードからだけ使う）。WebMock の「`src/core` は Three.js に依存しない」と同じ決まり
- `CUBELITHCore` のファイルは WebMock の `src/core` と 1 対 1 に対応させる（`grid.ts` → `Grid.h` / `Grid.cpp`）。関数名も揃え、移植元を追えるようにする
- `CUBELITH` は `CUBELITHCore` に依存する。人が調整する値（`UPROPERTY`）・アクタ・ウィジェットの基底クラスはこちらに置く
- 同じ結果を出すための注意（RULES.md 3.6）:
  - mulberry32 は `uint32` で計算し、[0, 1) への変換は `double` で行う（JS の number は倍精度）
  - JS の `Map` / `Set` は入れた順に回るが、`TMap` / `TSet` は順序が保証されない。順序が結果に効くところは `TArray` で持つ
  - JS の `Array.prototype.sort` は安定ソート。移植では `Algo::StableSort` を使う

### 7.2 座標系

- ロジック（`CUBELITHCore`）は Web 版と同じ座標で持つ。ボクセルは整数座標 (x, y, z) で Y が上、向きは 0..23 の id（表も合成の順も `WebMock/src/core/grid.ts` と同じ）。照合データはそのまま比べられる
- UE の座標（Z が上・左手系・cm）へは描画のときだけ変換する。UE の (X, Y, Z) = (x, z, y) × ボクセルの大きさ。y と z を入れ替えると右手系の Y-up が左手系の Z-up に写り、形は鏡像にならない
- 向き（回転行列 R）を UE の回転に直すときも同じ入れ替え P を使う（R_UE = P · R · P）
- ボクセルの大きさは 100 cm（UE の標準の立方体と同じ）

### 7.3 テストと照合データ

- `CUBELITH`（描画・入力・UI）側の Automation Test は `Source/CUBELITH/Private/Tests/` に置き、名前は `CUBELITH.Render.<分野>.<内容>` にする（`Scripts/Test.ps1` の既定のフィルタ `CUBELITH.` で `CUBELITH.Core.*` と一緒に走る）
- ゲームロジックは UE の Automation Test で保証する。WebMock の `tests/` を移植する。テストは `Scripts/Test.ps1` で回す（ビルドしてから `UnrealEditor-Cmd.exe` を `-nullrhi` で起動し、書き出された結果の JSON で合否を決める）。Launcher 版のエンジンでそのまま使える
- 照合データ（RULES.md 3.6）: WebMock が書き出した JSON を `Source/CUBELITHCore/Private/Tests/Fixtures/` に置く（28 ファイル）
  - **JSON の形と作り方は `FIXTURES.md`**（照合データの形の正）。書き出しは WebMock の `npm run export:fixtures`（ルートからは `npm --prefix WebMock run export:fixtures`）
  - 対象: N と M のプリセット 25 通り × パズルの回転 2 通り × シード 3 個
  - 中身: 生成したピース（id と局所座標のボクセル。解答の絶対座標は解答配置から復元する）、解答配置、初期散らし（ピースごとの位置と向き）、散らし直し（ヒントの固定があるとき・ないとき）。加えて向きの表（24 通り）と、いくつかのシードでの乱数の出力列
  - 生成の途中ではなく結果全体を比べる。「生成への影響」がある R を反映するときに作り直す
  - Web 版側も同じデータとの一致を `WebMock/tests/fixtures.test.ts` で見ている（`src/core` の生成結果が変わったらそこで落ちる）

### 7.4 リポジトリ

- Git LFS は使わない。`.uasset` / `.umap` も普通の git で管理し、`.gitattributes` で `binary` を付ける（改行の変換と差分・マージをさせない）
- バイナリは変更のたびに丸ごと履歴に残り、リポジトリが太り続ける。そのため:
  - 1 ファイル 50 MB 未満に抑える（GitHub は 50 MB で警告し、100 MB を超えると push を拒否する）
  - HDRI は 2K 程度にする。元素材（`.exr`・`.psd`・録音した `.wav` の原本など）はリポジトリに入れない
  - アセットは意味のある変更のときだけ保存・コミットする
- `Binaries/`・`Intermediate/`・`Saved/`・`DerivedDataCache/`・`.vs/`・生成されるソリューションファイルなどは git 管理外

### 7.5 watch とエディタの同時利用

- **今は作業ツリーを分けない**（2026-09-26 に決定）。watch はルートの `develop` で回し、**watch を回している間はエディタを開かない**
  - 理由: エディタが開いていて Live Coding が有効だと、同じプロジェクトのコマンドラインのビルド（verify）が通らない。watch がブランチを切り替えると、エディタで開いているアセットとぶつかる
  - U1〜U4 は人がエディタを使う場面が少ないので、時間で分ければ足りる
- エディタでの作業を watch と並行したくなったら（遅くとも U5）、watch 用の作業ツリー（`git worktree`。例 `E:\Projects\TatApp\CUBELITH-watch`）に分ける。その場合の注意:
  - 同じブランチは 2 つの作業ツリーで同時に checkout できないので、watch は `develop` とは別の元ブランチで動く。AI の成果は人の作業ツリーでその元ブランチをマージして取り込み、仕様の変更は watch を止めてから watch の作業ツリーで `develop` をマージして渡す
  - `Auto_Tasks/` と `/auto-tasks-*` の実行場所が watch の作業ツリーに移り、`Binaries/`・`Intermediate/`・`WebMock/node_modules` も作業ツリーごとに要る。ルートの CLAUDE.md（「watch はこのルートで回す」）と `Source/CLAUDE.md` を書き換える
- verify のタイムアウトは watch.ps1 の既定 30 分。`Scripts/Test.ps1` はビルド込みで 1 分未満（U0 時点）

### 7.6 エディタの MCP

- UE 5.8 同梱の Unreal MCP（プラグイン `ModelContextProtocol`）と、ツールを提供するツールセットのプラグインを使う。どれも実験的な機能で、`.uproject` でエディタのターゲットだけに有効にしている（ゲームのビルドには入れない）
  - ツールセットは使うものだけを有効にする: `EditorToolset`（アクタ・プロパティ・ビューポート・PIE・ログ）・`AutomationTestToolset`・`ConfigSettingsToolset`・`UMGToolSet`・`NiagaraToolsets`・`SlateInspectorToolset`
  - 全部入りの `AllToolsets` は使わない（ゲーム機能・GAS・PCG なども有効になり、ゲーム機能の設定が無いという警告がエディタの起動のたびに出る）
- サーバーはエディタの中で動き、エディタを開くと自動で起動する（`Config/DefaultEditorPerProjectUserSettings.ini` の `bAutoStartServer`）。接続先は `http://127.0.0.1:8000/mcp`（`.mcp.json`）。認証は無く、ループバックからしか受け付けない
- エディタが開いているときだけ使える。そのため Auto_Tasks のタスクの完了条件（verify）には使わず、対話のセッションで補助に使う
- MCP で AI がアセットを変えても、`.uasset` の差分は読めない。「`.uasset` は人が作る」（0 章）は変えない

### 7.7 乱数シードと難易度の指定

RULES.md 3.1 の「検証のために外から指定できるようにする」を UE 版でどう行うか。シード（符号なし 32 bit）と難易度（空間サイズ N・ピース分割数 M・パズルの回転）を、どちらも **マップ URL のオプション**・**コマンドライン引数**・**`ACubelithGameMode` の `UPROPERTY`** の 3 通りで外から指定できる。実装は解釈の純粋関数（`Source/CUBELITH/Public/CubelithSeed.h` / `CubelithDifficulty.h`）と読み取り・適用（`ACubelithGameMode`）に分けてあり、解釈は Automation Test（`CUBELITH.Render.Seed.*` / `CUBELITH.Render.Difficulty.*`）で確かめている。

#### シード

生成（`GeneratePuzzle`）と初期散らし（`ScatterPlacements`）には同じ値が渡るので、シードが同じなら同じパズルと同じ散らばり方になる。決め方は次の優先順位で、上から順に見て最初に見つかった有効な値を使う。

| 順 | 指定の方法 | 書き方 | 読む場所 |
|---|---|---|---|
| 1 | マップ URL のオプション | `?seed=123` | `AGameModeBase::InitGame` で `UGameplayStatics::ParseOption` |
| 2 | コマンドライン引数 | `-CubelithSeed=123` | `FParse::Value(FCommandLine::Get(), ...)` |
| 3 | `ACubelithGameMode` の `UPROPERTY` | `Seed`（−1 = 指定なし） | エディタの Details / Blueprint の既定値 |
| 4 | （どれも無ければ）ランダム | — | 時刻で撒いた `FRandomStream` から 32 bit |

書き方の例:

- PIE のコンソール（`~`）で `open /Game/Maps/Main?seed=123`
- パッケージ版・エディタの起動引数にマップごと渡す: `CUBELITH.exe /Game/Maps/Main?seed=123`
- コマンドライン: `CUBELITH.exe -CubelithSeed=123`（マップを指定しないときはこちら）
- エディタで `ACubelithGameMode` の `Seed` を 123 にする（URL もコマンドラインも無いときに効く）

有効な値と不正値の扱い:

- 有効なのは **0..4294967295 の 10 進整数だけ**（RULES.md 3.1 の符号なし 32 bit）。符号（`+1` / `-1`）・前後の空白・`12a` のような途中まで数字・16 進は受け付けない
- **不正な値は丸めずに無視して次の優先順位へ落とす**（Web 版と同じ。`WebMock/src/ui/params.ts` の `parseSeedParam`）。範囲外を丸めると `?seed=-1` と `?seed=0` が同じ盤面になり、「指定した値と盤面の対応」が壊れるため
- 無視したときは `LogCubelith` に警告を 1 行出す。実際に使ったシードとその経路（URL オプション / コマンドライン / プロパティ / ランダム）も毎回ログに出るので、ランダムに引いた盤面も後から再現できる
- 解釈: `?seed=`（値が空）は「指定なし」と区別できない（`ParseOption` は未指定でも空文字を返す）ので、警告を出さずに次へ落とす。`Seed` の負の値も同じく「指定なし」

Web 版の `?seed=` と同じオプション名・同じ範囲にしてあるので、**同じ N / M / パズルの回転と同じシードなら Web 版と UE 版で同じパズルが出る**（RULES.md 3.6）。

#### 難易度（N / M / パズルの回転）

RULES.md 3.1 の 3 項目を、シードと同じ 3 通りの方法で指定できる。優先順位も同じ（URL オプション > コマンドライン > `UPROPERTY`）だが、**項目ごとに独立して決まる**ので `?n=` だけ指定すれば M とパズルの回転は `UPROPERTY` の値がそのまま使われる。シードと違って「ランダム」の段は無い（`UPROPERTY` が必ず値を持つので、そこが最後の受け皿になる）。

| 項目 | URL オプション | コマンドライン引数 | `UPROPERTY` | 有効な値 | 不正な値 / 範囲外 |
|---|---|---|---|---|---|
| 空間サイズ N | `?n=4` | `-CubelithN=4` | `SpaceSize`（既定 3） | 3..7 の整数 | 読めなければ次の順位へ / 範囲外は 3..7 に丸める |
| ピース分割数 M | `?m=8` | `-CubelithM=8` | `PieceCount`（既定 4） | RULES.md 3.1 の N ごとの 5 段のプリセット | 読めなければ次の順位へ / プリセット外は最も近い段へ寄せる（同じ距離なら小さい方） |
| パズルの回転 | `?rot=1` | `-CubelithRotation=1` | `bAllowRotation`（既定 なし） | `1` / `true` / `on` / `yes`（あり）、`0` / `false` / `off` / `no`（なし）。大文字小文字は問わない | 読めなければ次の順位へ |

書き方の例:

- PIE のコンソール（`~`）で `open /Game/Maps/Main?seed=123&n=4&m=8&rot=1`（N=4・M=8・パズルの回転あり）
- 回転の操作（U3）を確かめたいときは `open /Game/Maps/Main?rot=1`（N と M は `UPROPERTY` の既定のまま）
- パッケージ版・エディタの起動引数にマップごと渡す: `CUBELITH.exe /Game/Maps/Main?n=4&m=8&rot=1`
- コマンドライン: `CUBELITH.exe -CubelithSeed=123 -CubelithN=4 -CubelithM=8 -CubelithRotation=1`（マップを指定しないときはこちら）

有効な値と不正値の扱い:

- N と M は 10 進整数だけ（先頭の符号 `+4` / `-1` は付いていてもよい）。`12a`・`1.5`・`0x10`・JavaScript の安全な整数（2^53 − 1）を超える桁数は読めないので無視して次の順位へ落ちる（Web 版 `WebMock/src/ui/params.ts` の `parseIntParam` と同じ）。前後の空白はシードと同じく受け付けない（マップ URL のオプションもコマンドラインも空白でトークンが切れるので、混じるとしたら打ち間違いのときだけ）
- **読めたが範囲外 / プリセット外の値は、無視ではなく寄せる**（ここがシードとの違い）。Web 版の `?n=` / `?m=` も丸める（`clampInt` / `WebMock/src/ui/difficulty.ts` の `nearestPreset`）し、難易度には「指定した値と盤面の 1 対 1 対応」を守る必要が無いため。例: `?n=9` → 7、`?m=100`（N=4）→ 12、`?m=7`（N=4）→ 6
- N を寄せた結果として M のプリセットが変わるので、M は N が決まったあとに寄せる（`?n=9` だけ指定すると N=7 になり、`PieceCount` の既定 4 は N=7 のプリセットの最小 7 へ寄る）
- 無視したとき・寄せたときは `LogCubelith` にそれぞれ 1 行の警告を出す。実際に使った N / M / 回転とその経路（URL オプション / コマンドライン / プロパティ）も毎回ログに出る
- 解釈: `?n=`（値が空）は「指定なし」と区別できないので、シードと同じく警告を出さずに次へ落とす
- 解釈: `?n=` / `?m=` は Web 版の URL クエリと同じ名前。Web 版の URL クエリに無い「パズルの回転」は UE 版で `?rot=` と決めた。コマンドラインは `-CubelithSeed=` と揃えて `-Cubelith` の前置きを付ける（`-n=` のような一般的すぎる名前はエンジンや他の機能とぶつかりうる）

画面（タイトル / 難易度選択）で難易度を選ぶ仕組みは U4 で足した（4 章の「画面（UI）」節）。**外部指定はタイトルの初期選択になり**（優先順位は 外部指定 > セーブの「最後に選んだ難易度」> `UPROPERTY` の既定）、「開始」を押したときはタイトルで選ばれている値で始まる。この口は検証用に残す。

## 8. 実装の段階（マイルストーン）

| 段階 | 内容 | 担当 | 完了の目安 |
|---|---|---|---|
| U0 | 雛形: `.uproject` と C++ モジュール 2 つ（7.1）・テスト、`.gitignore` / `.gitattributes`（7.4）、エディタの MCP（7.6）、UE 用の CLAUDE.md（`Source/CLAUDE.md`。コマンド・開発ルール・タスクの切り方）。2026-09-26 に完了 | 人と AI | コマンドラインでビルドとテストが通る |
| U1 | ゲームロジックの移植: RULES.md 3 章（グリッド・向き・乱数・生成・散らし・クリア判定・スナップ・固定とヒント）+ テスト + 照合データ | AI | テストが通り、照合データと一致する |
| U2 | 描画: ピースを ISM で表示、散らばった初期配置、軌道カメラ（マテリアルは仮）、シードの外部指定（7.7）。7.2 の座標変換・`ACubelithGameMode`・`ACubelithPuzzleActor`・`ACubelithOrbitPawn` が入った | AI | 生成結果が見える |
| U3 | 操作: 選択・グリッド移動・90 度回転・クリア検知（演出なし）、難易度の外部指定（7.7）。`ACubelithPlayerController`（ポーリング入力・ライントレースでのピックと選択・ドラッグ移動・2 本指のジェスチャ）と、`ACubelithPuzzleActor` の選択の強調 / 自由回転の見せ方（`SetFreeRotation`）・`ACubelithGameMode` のクリアの仮表示が入った。回転は「あり」の盤面（`?rot=1` / `-CubelithRotation=1`）でだけ効き、タッチは 2 本指のスワイプ / ひねり。マウスの右ボタンのドラッグは仮の手段で、**U4 で HUD の回転モードのトグルへ置き換えた**（4 章の「プレイ中 HUD」節。回転ギズモは作らない） | AI | 手でクリアできる |
| U4 | 手触り: スナップと効果音、HUD、画面での難易度選択、固定・ヒント・次の問題、セーブ（10 章）。スナップと効果音・セーブの形と読み書き・タイトル / 難易度選択の画面と画面の切り替え・`ACubelithGameMode` のセッション（作る / 畳む / 作り直す）・固定 / 固定解除とヒントと散らし直し・プレイ中 HUD（残りピース数・回転モードのトグル・固定・散らし直す・ヒント・次の問題・難易度へ戻る）と回転モードのドラッグが入った（4 章の「スナップ」「固定の鍵アイコン」「セーブ」「画面（UI）」「プレイ中 HUD」節）。クリア画面とセーブの保存のタイミングは残り | AI と人 | 一通り遊べる |
| U5 | 演出と質感: クリア時の発光・融合・パーティクル・カメラ旋回、すりガラス | 人と AI | 見せられる |
| U6 | 最適化とモバイル: 実機で目標の fps、タッチ操作の調整 | 人と AI | 実機で遊べる |

## 9. 受け入れ条件（verify に使えるもの）

- ビルドとテストがコマンドラインで通ること（`Scripts/Build.ps1` / `Scripts/Test.ps1`）
- ゲームロジック: `../WebMock/SPEC.md` 9 章と同じ性質（生成の網羅・連結、判定、向き）を満たし、照合データと一致すること
- 見た目・手触りは機械判定できないので、人が実機で確認する

## 10. スコープ（初回リリース）

- 入れる: セーブ。ルールは RULES.md 3.8（R3）。UE 版の保存の手段とタイミングは 4 章
- 未定: ランキング・課金・広告、対応する言語、iOS に取りかかる時期、ストアに出す時期
