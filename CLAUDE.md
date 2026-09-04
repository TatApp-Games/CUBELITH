# Crystal Assemble (仮) — Web 版試作

プロシージャル 3D 組み立てパズルのブラウザ試作。UE5 モバイル版に先立ち、生成アルゴリズム・操作感・演出をブラウザで検証する。**仕様の正は `SPEC.md`**。ここには開発の進め方だけを書く。

## 技術スタック

- TypeScript（strict）+ Vite + Three.js。UI は素の HTML / CSS
- テストは Vitest（`tests/`）。対象は `src/core`（ゲームロジック）
- 依存はこの 4 つ（`three` / `vite` / `typescript` / `vitest`）。足すときは理由をこの節に書く

## コマンド

```
npm install        依存の導入（初回）
npm run dev        開発サーバ（http://localhost:5173）
npm test           Vitest を 1 回実行（watch なし）
npm run build      型チェック（tsc --noEmit）+ 本番ビルド（dist/）
```

## URL クエリ（検証用）

| クエリ | 効果 |
| --- | --- |
| `?seed=<0..4294967295>` | 生成シードを固定する（SPEC.md 3.1）。不正値は無視して乱数にフォールバック |
| `?n=<3..7>` / `?m=<数>` | タイトルの初期選択。範囲外は丸める |
| `?lite=1` / `?lite=0` | 軽量描画モードの強制 on / off。無指定は端末判定（`src/render/quality.ts`） |
| `?fps=1` | 簡易 FPS / ドローコール表示 |

読み取りと不正値の扱いは `src/ui/params.ts`（純粋関数・テスト付き）に集約する。

## ディレクトリ

```
src/core/     純粋なゲームロジック。Three.js を import しない。生成・向き・クリア判定・スナップ候補・乱数
src/render/   Three.js。シーン・マテリアル・演出
src/input/    ポインタ / タッチ入力 → ゲーム操作
src/ui/       画面と HUD（HTML）
tests/        Vitest。core の全公開関数にテストを付ける
```

座標は `Vec3 = { x, y, z }`（読み取り専用のオブジェクト）で統一する。向きは 0..23 の整数 id で、`composeOrientation(a, b)` は「a を適用してから b」の順（`src/core/grid.ts`）。
ピースの局所原点は重心に最も近いボクセル（同点は座標の辞書順で最小）。`createPiece` / `normalizePiece` がそこを (0,0,0) に揃える（`src/core/piece.ts`）。

## 開発ルール

1. **`src/core` は Three.js に依存しない**。生成・判定・スナップの正しさは `npm test` で保証する。core に関数を足したらテストも足す
2. `npm test` と `npm run build` が通る状態でしか作業を終えない
3. 座標系: ボクセルは整数座標 `(x, y, z)`、Y が上。ピースの向きは 90 度単位の 24 通りを整数 id で扱う（`src/core/grid.ts` に集約）
4. 描画は 1 ピース = 1 `InstancedMesh`。ボクセルごとに Mesh を作らない
5. スマホで動くことを常に意識する。`devicePixelRatio` は 2 で上限、ポストプロセスは最小限
6. 仕様に無いことを足さない。曖昧な点は最も自然な解釈を選び、コード内コメントか PR 説明に「解釈:」として残す
7. 文書とコメントは日本語。識別子は英語

## 進め方（Auto_Tasks による自動実装）

- このプロジェクトは Auto_Tasks の要望駆動の新運用で開発する。人は要望をテキストにして `Auto_Tasks/00_要望/` に置く（または `/auto-tasks-request <パス>`）。運用の詳細は `Utility/自動化運用/Auto_Tasks運用ガイド.md`
- 要望は「SPEC.md の内容で Web モックを作成して」のように仕様全体を指してよい。その場合、分解は **`SPEC.md` 8 章のマイルストーン順（M1 → M2 → …）にタスクを切る**。M0（雛形）は準備済み。1 タスクは 1 マイルストーン以下の粒度にする
- タスクの verify は `npm test`（core の変更）または `npm run build`（描画・UI の変更）。見た目・手触りは verify 省略で人が確認する
- scope の目安: core の作業は `src/core/,tests/`、描画は `src/render/,src/main.ts`、UI は `src/ui/,index.html`
- watch の元ブランチは `develop`。`main` の上で watch を回さない（常時マージのため）。`main` へは人が確認してからマージする
- `Auto_Tasks/` は git 管理外（.gitignore 済み）
