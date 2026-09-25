# CUBELITH — Web 版 実装仕様

ステータス: 初版（2026-09-04）。2026-09-25 にゲームルールを `../Docs/RULES.md` へ切り出し、この文書には Web 版の実装仕様だけを残した。

- **ゲームルールの正は `../Docs/RULES.md`**（Web 版・UE 版で共通）。この文書は「それを Web でどう実装するか」の正
- 章番号は RULES.md と共通の番号体系。この文書は 0・4・7〜10 章を持ち、1・2・3・5・6 章（ルール）は RULES.md にある。コード中の「SPEC.md 4 章」「RULES.md 3.3」などの参照はこの番号を指す
- 最初の仕様書（UE5 モバイル版として書かれた原典）は `../Docs/ORIGIN.md`、UE 版の実装仕様は `../Docs/SPEC_UE.md`

## 0. 目的と位置づけ

- 本来の製品は UE5 のモバイルアプリ（`../Docs/SPEC_UE.md`）。その前段として、**ゲームロジックと手触りをブラウザで試作**し、生成アルゴリズム・操作感・演出の設計を検証する
- ルールや操作感の試行錯誤は **Web 版で先に行い**、確定したものを UE 版へ移す（RULES.md「運用」）。ルールを変えるときは RULES.md と Web 版の実装を同じコミットで変え、RULES.md の変更履歴に 1 行足す
- 開発の 9 割は生成 AI（Auto_Tasks の自動実装）が行う。人は要望を書き、結果を触って評価する
- 見た目（すりガラス質感）は「Web で出せる範囲で近づける」。UE 版のマテリアルを再現することが目的ではない

| 項目 | 内容 |
|---|---|
| プラットフォーム | ブラウザ（PC の Chrome / Edge を基準に、スマホの Safari / Chrome でも動くこと） |
| 技術 | TypeScript + Vite + Three.js。ロジックは Three.js に依存しない純粋な TS で書く（7 章） |

## 4. 描画と演出の実装（Web への写像）

原典（`../Docs/ORIGIN.md`）の UE の手段と、RULES.md のルールを Web の実装手段に読み替える。

| 原典の手段 / ルール | Web 版の実装手段 |
|---|---|
| Instanced Static Mesh でブロック群を描画 | `THREE.InstancedMesh`。**1 ピース = 1 InstancedMesh**（ボクセル数分のインスタンス）。ドローコールはピース数 + α に抑える |
| 動的ライトはディレクショナル 1 灯 + HDRI 環境光 | `DirectionalLight` 1 灯 + 環境マップ（`PMREMGenerator` で作った簡易 HDRI か `RoomEnvironment`）。影は無しか低解像度 1 枚 |
| すりガラス（Roughness 高めの半透明ガラス） | `MeshPhysicalMaterial`（`transmission` + `roughness` 0.4〜0.6 + `thickness`）を第一候補。重ければ半透明 `MeshStandardMaterial` + フレネル風の縁の発光に落とす |
| Fake 屈折（画面空間テクスチャの歪み） | 背景をレンダーターゲットに描き、法線で UV をずらして参照するカスタムシェーダ（`ShaderMaterial`）。**初版はスコープ外**（transmission で代用）。手触りが固まってから着手する |
| 内部発光コア（サイン波で明滅） | 各ボクセル中心に小さな発光メッシュ（InstancedMesh で 1 セット）。`emissiveIntensity` を `0.5 + 0.5 * sin(t)` で呼吸させる。ピースごとに位相をずらす |
| カメラ（RULES.md 3.3） | 注視点まわりの軌道カメラ（OrbitControls 相当） |
| スナップの効果音（RULES.md 3.5） | WebAudio の合成音。音源ファイルは作らない（10 章） |
| クリア演出の発光（RULES.md 5.2-1） | 材質の emissive だけでよい。ブルームがあれば併用する |
| クリア演出の融合（RULES.md 5.2-2） | 個別の InstancedMesh を非表示にして、単一の `BoxGeometry` を表示する |
| クリア演出のパーティクル（RULES.md 5.2-3） | `THREE.Points` + 簡単な速度・寿命更新 |

- 目標フレームレート: PC 60 fps、スマホ 30 fps 以上（N=7 / M=27 でも）
- 解像度: `devicePixelRatio` は上限 2 でクランプする

## 7. 技術構成

```
WebMock/
    index.html              エントリ（Vite）
    src/
        main.ts             起動。UI とレンダラをつなぐ
        core/               純粋なゲームロジック。Three.js を import しない（テスト対象）
            grid.ts         ボクセル座標・24 通りの向き・回転行列
            generate.ts     領域拡張による分割（RULES.md 3.2）
            piece.ts        ピースの表現と配置変換
            solve.ts        クリア判定（RULES.md 3.4）とスナップ候補（RULES.md 3.5）
            rng.ts          シード付き乱数
        render/             Three.js。シーン・マテリアル・演出
        input/              ポインタ / タッチ入力をゲーム操作に変換
        ui/                 HTML の画面と HUD
    tests/                  Vitest。core/ の全関数に対する単体テスト
    scripts/                Node で走らせる補助スクリプト。UE 版と照合するデータの書き出し（ブラウザには載らない）
    SPEC.md / CLAUDE.md
```

- 依存: `three`, `vite`, `typescript`, `vitest`, `vite-node`。これ以外を足すときは理由を CLAUDE.md に書く
- コマンド: `npm run dev`（開発サーバ）/ `npm test`（Vitest 一括）/ `npm run build`（型チェック込みの本番ビルド）/ `npm run export:fixtures`（UE 版と照合するデータの書き出し）
- `src/core` は **Three.js に依存しない**。生成・判定・スナップは全部ここに置き、テストで保証する
- `src/core` は UE 版の照合データの出どころでもある（RULES.md 3.6）。書き出しは `npm run export:fixtures`（出力先は `../Source/CUBELITHCore/Private/Tests/Fixtures/`）で、**JSON の形と作り方は `../Docs/FIXTURES.md`**。コミット済みのデータと `src/core` の今の出力が一致することは `tests/fixtures.test.ts` が見る。生成・初期散らしの結果が変わる変更は、RULES.md の変更履歴の「生成への影響」に書く（照合データを作り直す合図）
- UI は素の HTML + CSS（フレームワーク不使用）。Three.js のキャンバスに重ねる（RULES.md 6 章の実装）
- RULES.md 3.1 のシードの指定は URL クエリ `?seed=` で行う。検証用のクエリの一覧は CLAUDE.md、読み取りと不正値の扱いは `src/ui/params.ts`
- セーブ（RULES.md 3.8）は `localStorage` に JSON 1 件で持つ。キーは `cubelith.save.v1`（形を変えたら版を上げる。版が違うデータは読み捨てて既定から始める）。データの形・検証・更新は `src/ui/params.ts` と同じ立ち位置の `src/ui/save.ts`（DOM にも Three.js にも依存しない純粋関数 + 薄い入出力。テストは `tests/save.test.ts`）に集約し、画面との配線は `src/main.ts` が持つ
  - 起動時に 1 回読み、以後はメモリ上の内容を持ち回して変更のたびに書く。書くのは「開始 / 続きから」でセッションを始めたとき・配置が変わったとき（`createGame` の `onChange`）・固定 / 固定解除 / 散らし直し / ヒントの直後・クリアしたとき
  - 盤面として持つのは難易度・シード・配置・固定・残りピース数だけ。ピースの形は `generatePuzzle(n, m, seed)` で再現できる（RULES.md 3.6）ので保存しない
  - JSON が壊れている / 版が違う / 型や範囲が合わない / `localStorage` が使えない（プライベートモードでは参照自体が投げる）ときは、例外を外へ出さず既定値へ落とす。復元した配置が今の生成結果とピース id で食い違う場合は、その盤面を捨てて通常の散らしで始める
  - タイトル画面の初期値の優先順位は **URL クエリ > セーブ > 既定**（`?n=` `?m=` は検証用の明示指定なのでセーブで上書きしない）。シードは保存しない

## 8. 実装の段階（マイルストーン）

| 段階 | 内容 | 完了の目安 |
|---|---|---|
| M0 | 雛形。Vite + TS + Three.js が動き、`npm test` と `npm run build` が通る | 画面に立方体が 1 つ回っている（**準備済み**。`src/main.ts` と `src/core/rng.ts` + テスト） |
| M1 | core: グリッド・向き・生成（領域拡張）・クリア判定・シード乱数 + テスト | N=3〜7 / M の全組み合わせで連結・網羅・重複なしをテストが保証 |
| M2 | 描画: ピースを InstancedMesh で表示、散らばった初期配置、カメラ操作 | 生成結果が見える |
| M3 | 操作: 選択・グリッド移動・90 度回転・クリア検知（演出なし） | 手でクリアできる |
| M4 | 手触り: マグネットスナップ + SE、HUD、難易度選択画面 | 一通り遊べる |
| M5 | 演出: クリア時の発光・融合・パーティクル・カメラ旋回。すりガラス質感の調整 | 見せられる |
| M6 | 最適化とスマホ対応: 30 fps 以上、タッチ操作、`?seed=` | スマホで遊べる |

要望が仕様全体を指す場合（「SPEC.md の内容で Web モックを作成して」）、分解はこの表の順に M1 から切り、1 タスクは 1 マイルストーン以下の粒度にする。後続のマイルストーンは先行の成果物（`src/core` の API など）に依存するので、タスクの詳細に前提を書く。

## 9. 受け入れ条件（verify に使えるもの）

- `npm test` が通ること（core の単体テスト）。とくに:
  - 生成: 任意の N ∈ [3,7]・有効な M・任意のシードで、全ボクセルがちょうど 1 つのピースに属し、各ピースが連結している
  - 判定: 解答配置に戻せばクリアになり、1 ピースを 1 マスずらすとクリアにならない
  - 向き: 24 通りの向きが相異なり、4 回同じ軸で回すと元に戻る
- `npm run build` が通ること（型エラー 0）
- 見た目・手触り（質感・スナップの気持ちよさ・演出）は機械判定できないので verify を省略し、人が確認する

## 10. スコープ外（Web 版では作らない）

- UE5 / C++ / Blueprint に関するもの一切
- iOS / Android のネイティブアプリ化、ストア配信
- ランキング、課金
- 本物の屈折や重いポストプロセス（初版）。Fake 屈折は M5 以降の検討事項
- 音源ファイルの制作（合成音で代用）
