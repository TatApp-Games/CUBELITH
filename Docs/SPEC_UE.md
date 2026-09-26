# CUBELITH — UE 版 実装仕様

ステータス: U0 の着手前に決めることを反映（2026-09-26）。残る「未定」は 4 章（対象端末・鍵アイコン）と 10 章。

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
| 選択・スナップ候補の発光（RULES.md 3.3 / 5.1） | マテリアルのパラメータ | 人と AI |
| 固定の鍵アイコン（RULES.md 6 章） | 未定 | — |
| 軌道カメラ（`src/render/camera.ts`） | 注視点まわりの軌道カメラを C++ で。U2 で `ACubelithOrbitPawn`（`USpringArmComponent` + `UCameraComponent`）として実装し、`ACubelithGameMode` が開始時にパズルへ合わせる | AI |
| 入力（`src/input/`） | ライントレースでピースを選ぶ。純粋関数（`axisMapping` / `twoFingerGesture` など）はテストごと C++ へ移す。U2 のカメラ操作は、`InputMappingContext` / `InputAction` が `.uasset`（0 章）なので Enhanced Input を使わず、Tick で `APlayerController` から入力状態をポーリングして読む。人がアセットを作る段になれば Enhanced Input へ移せる。U3 で `ACubelithPlayerController` として実装（`PlayerTick` でのポーリング入力・押した瞬間のライントレースでのピックと選択・ドラッグでのグリッド移動・2 本指の 90 度回転）。`src/input` の純粋関数は `CubelithPickSamples` / `CubelithAxisMapping` / `CubelithTwoFingerGesture` / `CubelithFreeRotation` / `CubelithRotateInput`（`Source/CUBELITH/Public`）へ移した。回転中の 90 度に縛らない見せ方は `ACubelithPuzzleActor::SetFreeRotation`。**マウスの回転は右ボタンのドラッグ**（離した時点で最寄りの向きへ確定させる）で、これは HUD の回転モードのトグルと回転ギズモ（U4）までの仮の手段 | AI（感度の調整は人） |
| スナップの効果音（RULES.md 3.5） | MetaSounds | 人 |
| クリア演出（RULES.md 5.2） | 発光は Material Parameter Collection、パーティクルは Niagara（原典 5.2）、カメラの旋回は C++ | 人と AI |
| UI（RULES.md 6 章） | UMG。C++ の基底クラス（`BindWidget`）と、人が作るレイアウト。縦持ちの画面に合わせる | 人と AI |
| セーブ（RULES.md 3.8） | `USaveGame` を `UGameplayStatics::SaveGameToSlot` で保存する。保存先・保存する形・壊れたデータの扱いは下の「セーブ」節。盤面が変わるたびと、アプリがバックグラウンドに入るとき（`FCoreDelegates` のアプリのライフサイクルの通知）に書く。モバイルでは裏に回ったアプリが OS に終了させられることがあるため | AI |
| ライティング（原典 4.1） | ディレクショナルライト 1 灯 + HDRI | 人 |

- 目標フレームレート: 実機で 30 fps 以上（N=7 / M=27 でも）。対象端末は未定
- **早めに実機で確かめること**（U1 と並行して人が進める）: すりガラスの見た目と負荷、半透明の ISM でインスタンス同士の前後関係が崩れないか（インスタンス単位では並び替えられない）、Fake 屈折がモバイルで成り立つか

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

**まだ繋いでいないこと**: 保存のタイミング（盤面が変わるたび・バックグラウンドに入るとき）と「続きから」・クリア回数の表示は後続タスクで `ACubelithGameMode` から繋ぐ。ここまでは形と純粋関数・スロットへの読み書きの器だけ。Automation Test（`CUBELITH.Render.Progress.*` / `CUBELITH.Render.Save.*`）は実際のスロットへ読み書きしない（エディタの `Saved/` を汚さないため）ので、検証・更新の純粋関数を直接呼んで確かめている

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

画面（タイトル / 難易度選択）で難易度を選ぶ仕組みは U4 で足す。ここで足したのは外から指定する口だけで、U4 が入ってもこの口は検証用に残す。

## 8. 実装の段階（マイルストーン）

| 段階 | 内容 | 担当 | 完了の目安 |
|---|---|---|---|
| U0 | 雛形: `.uproject` と C++ モジュール 2 つ（7.1）・テスト、`.gitignore` / `.gitattributes`（7.4）、エディタの MCP（7.6）、UE 用の CLAUDE.md（`Source/CLAUDE.md`。コマンド・開発ルール・タスクの切り方）。2026-09-26 に完了 | 人と AI | コマンドラインでビルドとテストが通る |
| U1 | ゲームロジックの移植: RULES.md 3 章（グリッド・向き・乱数・生成・散らし・クリア判定・スナップ・固定とヒント）+ テスト + 照合データ | AI | テストが通り、照合データと一致する |
| U2 | 描画: ピースを ISM で表示、散らばった初期配置、軌道カメラ（マテリアルは仮）、シードの外部指定（7.7）。7.2 の座標変換・`ACubelithGameMode`・`ACubelithPuzzleActor`・`ACubelithOrbitPawn` が入った | AI | 生成結果が見える |
| U3 | 操作: 選択・グリッド移動・90 度回転・クリア検知（演出なし）、難易度の外部指定（7.7）。`ACubelithPlayerController`（ポーリング入力・ライントレースでのピックと選択・ドラッグ移動・2 本指のジェスチャ）と、`ACubelithPuzzleActor` の選択の強調 / 自由回転の見せ方（`SetFreeRotation`）・`ACubelithGameMode` のクリアの仮表示が入った。回転は「あり」の盤面（`?rot=1` / `-CubelithRotation=1`）でだけ効き、タッチは 2 本指のスワイプ / ひねり、マウスは右ボタンのドラッグ（仮の手段）。スナップ・HUD・回転モードのトグル・回転ギズモは U4 | AI | 手でクリアできる |
| U4 | 手触り: スナップと効果音、HUD、画面での難易度選択、固定・ヒント・次の問題、セーブ（10 章） | AI と人 | 一通り遊べる |
| U5 | 演出と質感: クリア時の発光・融合・パーティクル・カメラ旋回、すりガラス | 人と AI | 見せられる |
| U6 | 最適化とモバイル: 実機で目標の fps、タッチ操作の調整 | 人と AI | 実機で遊べる |

## 9. 受け入れ条件（verify に使えるもの）

- ビルドとテストがコマンドラインで通ること（`Scripts/Build.ps1` / `Scripts/Test.ps1`）
- ゲームロジック: `../WebMock/SPEC.md` 9 章と同じ性質（生成の網羅・連結、判定、向き）を満たし、照合データと一致すること
- 見た目・手触りは機械判定できないので、人が実機で確認する

## 10. スコープ（初回リリース）

- 入れる: セーブ。ルールは RULES.md 3.8（R3）。UE 版の保存の手段とタイミングは 4 章
- 未定: ランキング・課金・広告、対応する言語、iOS に取りかかる時期、ストアに出す時期
