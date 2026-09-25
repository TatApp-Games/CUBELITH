# CUBELITH / Crystal Assemble

プロシージャル 3D 組み立てパズル。**この CLAUDE.md はリポジトリ全体の入口**で、各サブプロジェクトの詳細（コマンド・開発ルール・タスクの切り方）はそれぞれの `CLAUDE.md` に置く。ルートには特定のサブプロジェクト専用のファイル（`package.json` など）を置かない。

## ディレクトリ構成

```
CrystalAssemble.md    UE5 版（本番）のゲーム仕様書
WebMock/              Web 版試作（TypeScript + Vite + Three.js）。詳細は WebMock/CLAUDE.md、仕様の正は WebMock/SPEC.md
Auto_Tasks/           自動実装体制の作業フォルダ（git 管理外）
```

ルートは将来 UE5 プロジェクトのルートになる（`.uproject` などはルート直下に置く）。

## リポジトリ

- git リポジトリは**このルート 1 本**。`WebMock/` 配下に `.git` を作らない（2026-09-21 に WebMock のローカルリポジトリをここへ統合した。統合前の `auto/*` ブランチは `../WebMock-history-260921.bundle` に保管）
- ブランチは `develop`（作業）と `main`（人が確認してマージ）。リモートは `origin`（TatApp-Games/CrystalAssemble）
- 統合前（2026-09-21 以前）の `WebMock/` のファイル別履歴は**旧パス**で引く（例: `git log --full-history -- src/core/grid.ts`）。`git blame WebMock/src/core/grid.ts` は旧パスまで遡るのでそのまま使える

## 進め方（Auto_Tasks による自動実装）

- 要望駆動の新運用で開発する。人は要望をテキストにして `Auto_Tasks/00_要望/` に置く（または `/auto-tasks-request`）。運用の詳細は `Utility/自動化運用/Auto_Tasks運用ガイド.md`
- **watch はこのルートで回す**（`Auto_Tasks/` はルート直下、git 管理外）。元ブランチは `develop`。`main` の上で watch を回さない
- scope はルートからの相対パスで書き、verify はルートで実行される前提で書く
- サブプロジェクトの要望の切り方・scope・verify の具体例は、そのサブプロジェクトの `CLAUDE.md` の「進め方」節に従う

## 作業前に読むもの

`WebMock/` 配下を触るタスク（要望の分解を含む）では **`WebMock/CLAUDE.md` を必ず読む**（コマンド・技術スタック・座標系・開発ルール・タスクの切り方がそこにある）。
