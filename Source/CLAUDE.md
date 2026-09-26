# CUBELITH — UE 版

UE5 のモバイル版（本番）。**ゲームルールの正は `../Docs/RULES.md`（Web 版と共通）、UE 版の実装仕様の正は `../Docs/SPEC_UE.md`、照合データの形の正は `../Docs/FIXTURES.md`**。ここには開発の進め方だけを書く。UE 版はルート（`CUBELITH.uproject`・`Config/`・`Content/`・`Source/`・`Scripts/`）全体で、この文書は `Source/` に置いてある。

## 技術スタック

- UE 5.8（Launcher 版。`C:\Program Files\Epic Games\UE_5.8`）と C++。Visual Studio 2026（MSVC 14.44）
- テストは Automation Test（`Misc/AutomationTest.h`）。エディタのコマンドラインで回す
- エディタの MCP（Unreal MCP）は `Docs/SPEC_UE.md` 7.6。エディタを開いているときだけ使える

## コマンド

リポジトリルートで実行する（pwsh 7）。

```
pwsh -NoProfile -File Scripts/Build.ps1                              エディタ用ターゲットをビルド（増分で 20 秒前後）
pwsh -NoProfile -File Scripts/Test.ps1                               ビルドしてから CUBELITH のテストを全部回す（40 秒前後）
pwsh -NoProfile -File Scripts/Test.ps1 -Filter CUBELITH.Core -NoBuild   テスト名の前方一致で絞る・ビルドを省く
pwsh -NoProfile -File Scripts/OpenEditor.ps1                         エディタで開く
```

- **エディタを開いたままだと Build / Test はビルドで失敗する**（Live Coding）。エディタを閉じてから実行する
- Test は失敗が 1 件でもあるか、テストが 1 件も見つからなければ終了コード 1。結果は `Saved/Automation/CommandLine/`（`Report/index.json` とログ `Test.log`）
- エンジンの場所は `.uproject` の `EngineAssociation`（`"5.8"`）からレジストリで引く。別の場所にあるなら環境変数 `UE_ROOT` で指定する
- エディタは Launcher・`.uproject` のダブルクリック・`Scripts/OpenEditor.ps1` のどれから開いてもよい。**「プロジェクトを変換」を求められたら変換しない**（`EngineAssociation` がこの PC でしか通じない GUID に書き換わる）。出るのはエンジンが自分を `"5.8"` と認識できていないときで、原因は Launcher の一覧 `C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat` に `UE_5.8` が無いこと（2026-09-26 に手で足して直した）
- コミットの前に `EngineAssociation` が `"5.8"` のままか確かめる。GUID になっていたら `"5.8"` に戻す

## ディレクトリ

```
Source/
    CUBELITHCore/         ゲームロジック（RULES.md 3 章）。WebMock の src/core に対応
        Public/           他のモジュールに見せるヘッダ（CUBELITHCORE_API）
        Private/          実装
        Private/Tests/    Automation Test。Fixtures/ は照合データ（手で編集しない）
    CUBELITH/             ゲーム本体（描画・入力・UI・セーブ）
Config/                   設定（テキスト。エディタの Project Settings もここに保存される）
Content/                  人が作るアセット（.uasset / .umap）
Scripts/                  ビルド・テスト・エディタ起動
```

## 開発ルール

1. **`CUBELITHCore` は `Core` モジュールにだけ依存する**（`CoreUObject`・`Engine` を足さない。UObject・`UPROPERTY` を使わない）。`Json` は照合データを読むテストからだけ使う（`Docs/SPEC_UE.md` 7.1）
2. `CUBELITHCore` のファイルは `WebMock/src/core` と 1 対 1 に対応させ（`grid.ts` → `Grid.h` / `Grid.cpp`）、関数名も揃える。**挙動は WebMock の実装が正**（RULES.md 3.6）。移植で結果がずれやすい点（`uint32` と `double`・`TMap` の順序・安定ソート）は `Docs/SPEC_UE.md` 7.1
3. ロジックの座標は Web 版と同じ（Y が上の整数グリッド、向きは 0..23 の id）。UE の座標への変換は描画のときだけ（`Docs/SPEC_UE.md` 7.2）
4. テストは `Source/CUBELITHCore/Private/Tests/` に置き、名前は `CUBELITH.Core.<分野>.<内容>`、フラグは `EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter`。WebMock の `tests/` を移植し、照合データとの一致を確かめる。`CUBELITHCore` に関数を足したらテストも足す
5. `Scripts/Test.ps1` が通る状態でしか作業を終えない（描画・入力・UI だけの変更は `Scripts/Build.ps1`）
6. **`.uasset` / `.umap` を作らない・編集しない**（人が作る。`Docs/SPEC_UE.md` 0 章）。人が調整する値は `UPROPERTY(EditAnywhere)` で公開し、Blueprint はアセットの割り当てに留める。MCP でエディタを操作できても、アセットは変えない
7. 照合データ（`Private/Tests/Fixtures/`）は手で編集しない。作り直すときは `npm --prefix WebMock run export:fixtures`（`Docs/FIXTURES.md`）
8. **ルールは UE 側で変えない**。ルールの問題に気付いたら Web 版への要望にする（RULES.md「運用」）。移植を終えた R 番号は `Docs/SPEC_UE.md` 冒頭の「反映済み」に書く
9. 仕様（RULES.md / SPEC_UE.md）に無いことを足さない。曖昧な点は最も自然な解釈を選び、コード内コメントか作業の記録に「解釈:」として残す
10. 文書とコメントは日本語、識別子は英語（UE の命名規則: `F` / `U` / `A` / `E` / `I` の接頭辞）。ソースは BOM なしの UTF-8（UBT が MSVC に `/utf-8` を付ける）

## 進め方（Auto_Tasks による自動実装）

- 要望は「`Docs/SPEC_UE.md` の U1 を実施して」のように段階を指してよい。その場合、分解は **`Docs/SPEC_UE.md` 8 章の段階の順（U1 → U2 → …）にタスクを切る**。U0（雛形）は準備済み。1 タスクは 1 段階以下の粒度にする
  - U1 は `WebMock/src/core` のファイル単位で、依存の順（`rng` → `grid` → `piece` → `generate` → `solve` → `hint` → `game`）に切ると、1 タスクごとに照合データで確かめられる
- タスクの verify はルートで実行される。ロジック（`Source/CUBELITHCore/`）の変更は **`pwsh -NoProfile -File Scripts/Test.ps1`**、描画・入力・UI の変更は **`pwsh -NoProfile -File Scripts/Build.ps1`** と書く。見た目・手触りは verify 省略で人が確認する。**MCP（エディタ）は verify に使わない**
- scope はルートからの相対パスで書く。ロジックは `Source/CUBELITHCore/`、描画・入力・UI は `Source/CUBELITH/`。モジュールや設定を足すタスクは `CUBELITH.uproject`・`Config/`・`Source/*.Target.cs` を、「反映済み」を更新するタスクは `Docs/SPEC_UE.md` を加える
- **watch を回している間は、エディタを開かない**（ビルドが通らず、ブランチの切り替えがエディタで開いているアセットとぶつかる）
