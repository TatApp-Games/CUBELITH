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
| ピースの描画（`src/render/pieces.ts`） | 1 ピース = 1 `UInstancedStaticMeshComponent`（原典 4.1）。ボクセルの位置は 7.2 の変換で UE の座標へ直す | AI |
| すりガラス（原典 4.2） | 半透明マテリアル（Roughness 高め）+ Fake 屈折 | 人 |
| 内部発光コア（`src/render/glowCores.ts`） | Emissive をサイン波で明滅させるマテリアル。ピースごとの位相はインスタンスごとの値（Per-Instance Custom Data）で渡す | 人（値の受け渡しは AI） |
| 選択・スナップ候補の発光（RULES.md 3.3 / 5.1） | マテリアルのパラメータ | 人と AI |
| 固定の鍵アイコン（RULES.md 6 章） | 未定 | — |
| 軌道カメラ（`src/render/camera.ts`） | 注視点まわりの軌道カメラを C++ で | AI |
| 入力（`src/input/`） | Enhanced Input（タッチとマウス）+ ライントレースでピースを選ぶ。純粋関数（`axisMapping` / `twoFingerGesture` など）はテストごと C++ へ移す | AI（感度の調整は人） |
| スナップの効果音（RULES.md 3.5） | MetaSounds | 人 |
| クリア演出（RULES.md 5.2） | 発光は Material Parameter Collection、パーティクルは Niagara（原典 5.2）、カメラの旋回は C++ | 人と AI |
| UI（RULES.md 6 章） | UMG。C++ の基底クラス（`BindWidget`）と、人が作るレイアウト。縦持ちの画面に合わせる | 人と AI |
| セーブ（10 章） | `USaveGame` を `UGameplayStatics::SaveGameToSlot` で保存する。盤面が変わるたびと、アプリがバックグラウンドに入るとき（`FCoreDelegates` のアプリのライフサイクルの通知）に書く。モバイルでは裏に回ったアプリが OS に終了させられることがあるため | AI |
| ライティング（原典 4.1） | ディレクショナルライト 1 灯 + HDRI | 人 |

- 目標フレームレート: 実機で 30 fps 以上（N=7 / M=27 でも）。対象端末は未定
- **早めに実機で確かめること**（U1 と並行して人が進める）: すりガラスの見た目と負荷、半透明の ISM でインスタンス同士の前後関係が崩れないか（インスタンス単位では並び替えられない）、Fake 屈折がモバイルで成り立つか

## 7. 技術構成

コマンドの細部は U0 で試して、UE 用の CLAUDE.md に書く。

```
CUBELITH.uproject
Config/
Content/                  人が作るアセット
Source/
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

- ゲームロジックは UE の Automation Test で保証する。WebMock の `tests/` を移植する。テストはエディタのコマンドライン（`UnrealEditor-Cmd.exe` を `-nullrhi` などで起動）で回す。Launcher 版のエンジンでそのまま使える
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

### 7.5 watch の作業ツリー

- watch はルートとは別の作業ツリー（`git worktree`。`E:\Projects\TatApp\CUBELITH-watch`）で回す。人はルートの作業ツリーでエディタを開く
  - 理由: エディタが開いていて Live Coding が有効だと、同じプロジェクトのコマンドラインのビルドが通らない。watch がブランチを切り替えると、エディタで開いているアセットとぶつかる
- 同じブランチは 2 つの作業ツリーで同時に checkout できない。watch の作業ツリーが `develop` を持ち、人の作業ツリーは別のブランチで作業して `develop` と取り込み合う。手順は U0 で試して、UE 用の CLAUDE.md とルートの CLAUDE.md（「watch はこのルートで回す」）を書き換える
- `Auto_Tasks/` は watch の作業ツリーに置く。`Binaries/`・`Intermediate/` は作業ツリーごとに持つ
- verify のタイムアウトは watch.ps1 の既定 30 分。エンジンはビルド済みで、ビルドするのはプロジェクトのモジュールだけなので、この範囲に収まる見込み

### 7.6 エディタの MCP

- UE 5.8 同梱の Unreal MCP（プラグイン `ModelContextProtocol`）と、ツールを提供する `AllToolsets` を使う。どちらも実験的な機能で、`.uproject` でエディタのターゲットだけに有効にしている（ゲームのビルドには入れない）
- サーバーはエディタの中で動き、エディタを開くと自動で起動する（`Config/DefaultEditorPerProjectUserSettings.ini` の `bAutoStartServer`）。接続先は `http://127.0.0.1:8000/mcp`（`.mcp.json`）。認証は無く、ループバックからしか受け付けない
- エディタが開いているときだけ使える。そのため Auto_Tasks のタスクの完了条件（verify）には使わず、対話のセッションで補助に使う
- MCP で AI がアセットを変えても、`.uasset` の差分は読めない。「`.uasset` は人が作る」（0 章）は変えない

## 8. 実装の段階（マイルストーン）

| 段階 | 内容 | 担当 | 完了の目安 |
|---|---|---|---|
| U0 | 雛形: `.uproject` と C++ モジュール 2 つ（7.1）・テスト、`.gitignore` / `.gitattributes`（7.4）、watch の作業ツリー（7.5）、UE 用の CLAUDE.md（コマンド・開発ルール・タスクの切り方） | 人と AI | コマンドラインでビルドとテストが通る |
| U1 | ゲームロジックの移植: RULES.md 3 章（グリッド・向き・乱数・生成・散らし・クリア判定・スナップ・固定とヒント）+ テスト + 照合データ | AI | テストが通り、照合データと一致する |
| U2 | 描画: ピースを ISM で表示、散らばった初期配置、軌道カメラ（マテリアルは仮） | AI | 生成結果が見える |
| U3 | 操作: 選択・グリッド移動・90 度回転・クリア検知（演出なし） | AI | 手でクリアできる |
| U4 | 手触り: スナップと効果音、HUD、難易度選択、固定・ヒント・次の問題、セーブ（10 章） | AI と人 | 一通り遊べる |
| U5 | 演出と質感: クリア時の発光・融合・パーティクル・カメラ旋回、すりガラス | 人と AI | 見せられる |
| U6 | 最適化とモバイル: 実機で目標の fps、タッチ操作の調整 | 人と AI | 実機で遊べる |

## 9. 受け入れ条件（verify に使えるもの）

- ビルドとテストがコマンドラインで通ること（コマンドは U0 で決める）
- ゲームロジック: `../WebMock/SPEC.md` 9 章と同じ性質（生成の網羅・連結、判定、向き）を満たし、照合データと一致すること
- 見た目・手触りは機械判定できないので、人が実機で確認する

## 10. スコープ（初回リリース）

- 入れる: セーブ。ルールは RULES.md 3.8（R3）。UE 版の保存の手段とタイミングは 4 章
- 未定: ランキング・課金・広告、対応する言語、iOS に取りかかる時期、ストアに出す時期
