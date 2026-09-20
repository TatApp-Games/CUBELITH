# GEODIA / Crystal Assemble

プロシージャル 3D 組み立てパズル。**この CLAUDE.md はリポジトリ全体の入口**で、各サブプロジェクトの詳細はそれぞれの `CLAUDE.md` に置く。

## ディレクトリ構成

```
CrystalAssemble.txt   UE5 版（本番）のゲーム仕様書
WebMock/              Web 版試作（TypeScript + Vite + Three.js）。詳細は WebMock/CLAUDE.md、仕様の正は WebMock/SPEC.md
Auto_Tasks/           自動実装体制の作業フォルダ（git 管理外）
```

UE5 プロジェクトを作るときはルート直下に置く。

## リポジトリ

- git リポジトリは**このルート 1 本**。`WebMock/` 配下に `.git` を作らない（2026-09-21 に WebMock のローカルリポジトリをここへ統合した。統合前の `auto/*` ブランチは `../WebMock-history-260921.bundle` に保管）
- ブランチは `develop`（作業）と `main`（人が確認してマージ）。リモートは `origin`（TatApp-Games/CrystalAssemble）

## コマンド

ルートから打つ。実体は `WebMock/` に委譲される（ルートの `package.json` は委譲のみで依存を持たない）。

```
npm run setup      WebMock の依存を導入（初回）
npm run dev        開発サーバ（http://localhost:5173）
npm test           Vitest を 1 回実行（watch なし）
npm run build      型チェック（tsc --noEmit）+ 本番ビルド（WebMock/dist/）
```

## 進め方（Auto_Tasks による自動実装）

- 要望駆動の新運用で開発する。人は要望をテキストにして `Auto_Tasks/00_要望/` に置く（または `/auto-tasks-request`）。運用の詳細は `Utility/自動化運用/Auto_Tasks運用ガイド.md`
- **watch はこのルートで回す**（`Auto_Tasks/` はルート直下、git 管理外）。元ブランチは `develop`。`main` の上で watch を回さない
- Web 試作の要望は `WebMock/SPEC.md` が仕様の正。仕様全体を指す要望は SPEC.md 8 章のマイルストーン順（M1 → M2 → …）に切り、1 タスクは 1 マイルストーン以下の粒度にする
- **scope はルートからの相対パスで書く**。`WebMock/` を前置きすること（例: `WebMock/src/core/,WebMock/tests/`）
  - core の作業: `WebMock/src/core/,WebMock/tests/`
  - 描画: `WebMock/src/render/,WebMock/src/main.ts`
  - UI: `WebMock/src/ui/,WebMock/index.html`
- verify はルートから `npm test`（core の変更）または `npm run build`（描画・UI の変更）。見た目・手触りは verify 省略で人が確認する

## 作業前に読むもの

`WebMock/` 配下を触るタスクでは **`WebMock/CLAUDE.md` を必ず読む**（技術スタック・座標系・開発ルールがそこにある）。
