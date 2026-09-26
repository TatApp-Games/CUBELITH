# CUBELITH

プロシージャル 3D 組み立てパズル。**この CLAUDE.md はリポジトリ全体の入口**で、各サブプロジェクトの詳細（コマンド・開発ルール・タスクの切り方）はそれぞれの `CLAUDE.md` に置く。ルートには特定のサブプロジェクト専用のファイル（`package.json` など）を置かない。

## ディレクトリ構成

```
CUBELITH.uproject     UE5 版（本番）のプロジェクト。Config/・Content/・Source/・Scripts/ と合わせて、ルートが UE プロジェクトのルート
Config/
Content/
Source/               C++。UE 版の詳細は Source/CLAUDE.md
Scripts/              UE 版のビルド・テスト・エディタ起動
Docs/
    RULES.md          ゲームルールの正（Web 版・UE 版で共通）
    SPEC_UE.md        UE5 版（本番）の実装仕様。RULES.md をどこまで反映したかもここ
    FIXTURES.md       UE 版と照合するデータ（JSON）の形
    ORIGIN.md         最初の仕様書（UE5 モバイル版として書かれた原典。凍結）
WebMock/              Web 版試作（TypeScript + Vite + Three.js）。詳細は WebMock/CLAUDE.md、実装仕様は WebMock/SPEC.md
Auto_Tasks/           自動実装体制の作業フォルダ（git 管理外）
.mcp.json             UE エディタの MCP サーバー（エディタを開いているときだけ使える。Docs/SPEC_UE.md 7.6）
```

UE 版の構成は `Docs/SPEC_UE.md` 7 章、コマンド・開発ルール・タスクの切り方は `Source/CLAUDE.md`。

## 仕様書の構成

- **ルール**（プレイヤーから見える振る舞いと、同じシードで同じ結果を出すための決め事）は `Docs/RULES.md` だけに書く。**実装**（エンジンの機能への写像・マイルストーン・verify）は Web 版が `WebMock/SPEC.md`、UE 版が `Docs/SPEC_UE.md` に書く
- 章番号は 3 つの文書で共通の番号体系（RULES.md が 1・2・3・5・6 章、実装仕様が 0・4・7〜10 章）。コード中の「RULES.md 3.3」「SPEC.md 4 章」はこの番号を指す
- ルールや操作感の試行錯誤は **Web 版で先に行い**、確定したものを UE 版へ移す。ルールを変えるときは RULES.md と Web 版の実装を同じコミットで変え、RULES.md 末尾の変更履歴に R 番号付きで 1 行足す。UE 版は `Docs/SPEC_UE.md` の「反映済み」の R 番号からの差分を移植する（詳細は RULES.md「運用」）
- 同じ条件とシードなら Web 版と UE 版で同じパズルを出す（RULES.md 3.6）。生成の細部は `WebMock/src/core` の実装が正

## リポジトリ

- git リポジトリは**このルート 1 本**。`WebMock/` 配下に `.git` を作らない（2026-09-21 に WebMock のローカルリポジトリをここへ統合した。統合前の `auto/*` ブランチは `../WebMock-history-260921.bundle` に保管）
- ブランチは `develop`（作業）と `main`（人が確認してマージ）。リモートは `origin`（TatApp-Games/CUBELITH。2026-09-25 に CrystalAssemble から改名）
- 統合前（2026-09-21 以前）の `WebMock/` のファイル別履歴は**旧パス**で引く（例: `git log --full-history -- src/core/grid.ts`）。`git blame WebMock/src/core/grid.ts` は旧パスまで遡るのでそのまま使える

## 進め方（Auto_Tasks による自動実装）

- 要望駆動の新運用で開発する。人は要望をテキストにして `Auto_Tasks/00_要望/` に置く（または `/auto-tasks-request`）。運用の詳細は `Utility/自動化運用/Auto_Tasks運用ガイド.md`
- **watch はこのルートで回す**（`Auto_Tasks/` はルート直下、git 管理外）。元ブランチは `develop`。`main` の上で watch を回さない
- **watch を回している間は UE エディタを開かない**（verify のビルドが通らず、ブランチの切り替えがエディタとぶつかる。`Docs/SPEC_UE.md` 7.5）
- scope はルートからの相対パスで書き、verify はルートで実行される前提で書く
- サブプロジェクトの要望の切り方・scope・verify の具体例は、そのサブプロジェクトの `CLAUDE.md` の「進め方」節に従う

## 作業前に読むもの

- `WebMock/` 配下を触るタスク（要望の分解を含む）では **`WebMock/CLAUDE.md` を必ず読む**（コマンド・技術スタック・座標系・開発ルール・タスクの切り方がそこにある）
- ゲームのルールに関わるタスク（Web 版・UE 版とも）では **`Docs/RULES.md` を読む**
- UE 版（`CUBELITH.uproject`・`Config/`・`Content/`・`Source/`・`Scripts/`）を触るタスク（要望の分解を含む）では **`Source/CLAUDE.md` と `Docs/SPEC_UE.md` を必ず読む**
