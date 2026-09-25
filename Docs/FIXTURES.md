# CUBELITH — UE 版と照合するデータ（golden fixtures）

「同じ条件とシードなら Web 版と UE 版で同じパズルを出す」（RULES.md 3.6）ことを確かめるための JSON の形と作り方をまとめた文書。**照合データの形の正はこの文書**で、ルール（RULES.md）でも片方の実装仕様でもない。

- 出どころは `WebMock/scripts/fixtures/build.ts`。**中身の定義はコードが正**で、この文書は読み手（UE 版）のための説明。コードを変えたらここも直す
- 置き場所は `Source/CUBELITHCore/Private/Tests/Fixtures/`。読み手は UE 版のテスト（SPEC_UE.md 7.3）、書き手は Web 版（SPEC.md 7 章）
- 何を照合データで保証するかは RULES.md 3.6、UE 側の読み方の制約は SPEC_UE.md 7.1 / 7.2 にある

## 書き出し方

```
npm --prefix WebMock run export:fixtures      リポジトリルートから
npm run export:fixtures                       WebMock/ から
```

- 出力先は `Source/CUBELITHCore/Private/Tests/Fixtures/`（スクリプト自身の位置から解決するので、どのディレクトリから実行しても同じ場所に出る）
- **手で編集せず、必ず作り直す**。実行ごとに変わる値（日時・環境・バージョン）は一切入れないので、2 回実行しても 1 バイトも変わらない
- 書き出しは既存のファイルを上書きするだけで消さない。不要になったファイルの検出は `WebMock/tests/fixtures.test.ts` の責務
- 整形は `index.json` だけ indent 2、他は空白なしの 1 行。どのファイルも末尾に改行 1 つ（LF）
- 数値は整数だけで、浮動小数は書かない（桁の丸めで比べにくくなるのを避ける）

## ファイル一覧（28 ファイル）

| ファイル | 中身 |
| --- | --- |
| `index.json` | 目録。形式の版・定数・ファイル名の一覧・N と M の組み合わせ |
| `orientations.json` | 24 通りの向きの回転行列と合成表（RULES.md 3.3） |
| `rng.json` | いくつかのシードでの mulberry32 の出力列（RULES.md 3.6） |
| `puzzle_n{N}_m{M}.json` | N・M ごとのパズル 1 組（25 ファイル） |

## 共通の表し方

- **座標**は `[x, y, z]` の整数 3 要素配列。Y が上（ロジックの座標系。UE への変換は SPEC_UE.md 7.2）
- **配置**は `{ "pieceId": <整数>, "orientation": <0..23>, "position": [x, y, z] }`。キーはこの順で並ぶ
- **向き**は 0..23 の整数 id。0 が恒等（`WebMock/src/core/grid.ts` の `IDENTITY_ORIENTATION`）
- ピース id は `0..M-1`。ピースの配列・配置の配列はどれも M 件で、id の昇順に並ぶ

## index.json

```json
{
  "formatVersion": 1,
  "generator": "WebMock/scripts/exportFixtures.ts",
  "orientationCount": 24,
  "puzzleSeeds": [1, 20260926, 4294967295],
  "rngSeeds": [0, 1, 20260926, 4294967295],
  "rngStreamLength": 64,
  "hintCount": 2,
  "files": ["orientations.json", "..."],
  "combinations": [{ "n": 3, "m": 3, "file": "puzzle_n3_m3.json" }]
}
```

- `formatVersion`: JSON の形（キー・並び）を変えたら上げる。読み手は想定と違う版なら失敗させる
- `generator`: 書き出しの入口。どこで作られたデータか分かるようにしただけで、読み手は使わない
- `files`: `index.json` 以外の全ファイル名の昇順（ロケールに依らない素の文字列比較）
- `combinations`: N 昇順・その中で M 昇順の 25 件。`file` はそのまま開けるファイル名
- 他のキーは後述の定数と同じ値

## orientations.json

```json
{ "count": 24, "matrices": [[1,0,0, 0,1,0, 0,0,1]], "compose": [0] }
```

- `matrices[id]`: 向き `id` の回転行列を**行優先**で並べた 9 要素（`orientationMatrix(id)` と同じ）。要素は -1 / 0 / 1
- `compose[a * 24 + b]`: 「`a` を適用してから `b`」の向き id（`composeOrientation(a, b)` と同じ順）。順を逆にすると別の表になるので注意

## rng.json

```json
{ "streamLength": 64, "streams": [{ "seed": 0, "uint32": [], "nextInt24": [], "nextInt1000": [] }] }
```

- `streams` は `rngSeeds` の順。各列の長さは `streamLength`（64）
- `uint32`: mulberry32 の**生の 32 bit 値**。`next()` が返す `[0, 1)` は `uint32 / 4294967296`。UE 側は `uint32` のまま比べられる（`uint32` で計算し、`[0, 1)` への変換だけ `double` で行う。SPEC_UE.md 7.1）
- `nextInt24`: `nextInt(24)` の出力列。`nextInt1000`: `nextInt(1000)` の出力列
- 3 つの列はそれぞれ**新しい `createRng(seed)`** から取る（互いに乱数を食い合わせない）。読み手も列ごとに乱数を作り直して比べる

## puzzle_n{N}_m{M}.json

```json
{
  "n": 3,
  "m": 3,
  "cases": [
    {
      "seed": 1,
      "allowRotation": false,
      "pieces": [{ "id": 0, "voxels": [[0, 0, 0]] }],
      "solution": [{ "pieceId": 0, "orientation": 0, "position": [1, 1, 2] }],
      "scatter": [{ "pieceId": 0, "orientation": 0, "position": [2, -4, 1] }],
      "reshuffleWithoutHints": [{ "pieceId": 0, "orientation": 0, "position": [2, -4, 1] }],
      "hintPieceIds": [0, 1],
      "reshuffleWithHints": [{ "pieceId": 0, "orientation": 0, "position": [1, 1, 2] }]
    }
  ]
}
```

- `cases` は 6 件。並びは `allowRotation` が `false` → `true`、その中で `puzzleSeeds` の順
- `pieces[].voxels` は**局所座標**（局所原点が `(0, 0, 0)`。局所原点は重心に最も近いボクセルで、同点は座標の辞書順で最小。`WebMock/src/core/piece.ts`）。並びは座標の昇順（x → y → z）
  - **解答の絶対座標はここには無い**。`solution` の配置から復元する。向きが恒等なので `voxel + position` で、結果は立方体 `[0, N-1]³` を隙間なく覆う
- `solution`: 生成時の解答配置。向きは全ピース 0（恒等）、`position` は局所原点の絶対座標
- `scatter`: プレイ開始時の初期散らし（RULES.md 3.2-5）
- `reshuffleWithoutHints`: ヒントの固定が無いときの散らし直し。RULES.md 3.3「やり直し」のとおり `scatter` と同じ配置になる
- `hintPieceIds`: ヒント（RULES.md 3.7）で固定したピース id を、選ばれた順に並べたもの。長さは `hintCount` 以下
- `reshuffleWithHints`: `hintPieceIds` を固定したままの散らし直し。固定したピースは `solution` と同じ配置で残り、残りが散らし直される
- `allowRotation` が `false` のケースは、どの配置の配列も全ピースの `orientation` が 0（RULES.md 3.1「パズルの回転」なし）

## 各ケースの作り方

UE 側が同じ手順で照合できるよう、`buildCase(n, m, seed, allowRotation)`（`WebMock/scripts/fixtures/build.ts`）が呼ぶ順を書く。

1. `generatePuzzle(n, m, seed)` → `pieces` と `solution`
2. `scatterPlacements(pieces, n, seed, { allowRotation })` → `scatter`
3. `scatterPlacements(pieces, n, seed, { allowRotation, keep: [] })` → `reshuffleWithoutHints`
4. `hintCount`（2）回繰り返して `hintPieceIds` を作る。`current` は 2 の `scatter` から始める
   1. `pickHintPiece(current, solution, hintPieceIds)` で対象を選ぶ。`null`（未固定が 1 個以下）ならそこで打ち切る
   2. その id の配置を `solution` の配置に差し替えて `current` を更新し、id を `hintPieceIds` の末尾に足す
5. `hintPieceIds` の順に `solution` の配置を並べたものを `keep` にして `scatterPlacements(pieces, n, seed, { allowRotation, keep })` → `reshuffleWithHints`

ヒントは乱数を使わない（RULES.md 3.6）。散らしは呼び出しごとに `createRng(seed)` を作り直すので、`scatter` と 2 つの散らし直しは互いに乱数を食い合わせない。

## 対象の組み合わせと定数

- **N**: 3〜7、**M**: N ごとの 5 段のプリセット（RULES.md 3.1）の 25 通り。表は書き写さず `piecePresets(n)`（`WebMock/src/ui/difficulty.ts`）から作る
- **パズルの回転**: `false` / `true` の 2 通り
- **`puzzleSeeds`**: `[1, 20260926, 4294967295]`
  - 解釈: 小さい値・日付由来の値・`uint32` の最大値の 3 つにして、`seed >>> 0` まわりの取りこぼしを拾えるようにした
- **`rngSeeds`**: `[0, 1, 20260926, 4294967295]`（出力列だけは seed 0 も入れる）
- **`rngStreamLength`**: 64、**`hintCount`**: 2、**`orientationCount`**: 24
- ケースの総数は 25 × 2 × 3 = 150

値の定義は `WebMock/scripts/fixtures/build.ts` の `PUZZLE_SEEDS` / `RNG_SEEDS` / `RNG_STREAM_LENGTH` / `HINT_COUNT` / `FORMAT_VERSION`。

## UE 側での読み方

- ロジック（`CUBELITHCore`）の座標系は Web 版と同じなので、**JSON の値をそのまま比べられる**（UE の座標への変換は描画のときだけ。SPEC_UE.md 7.2）
- JSON の読み込みに使う `Json` モジュールは**テストのコードからだけ**使う（`CUBELITHCore` 本体は `Core` だけに依存する。SPEC_UE.md 7.1）
- 順序が結果に効くところは `TMap` / `TSet` ではなく `TArray` で持つ（同 7.1）

## 作り直す合図

`Docs/RULES.md` の変更履歴で「生成への影響」が**あり**の R を入れたとき。そのとき `npm --prefix WebMock run export:fixtures` をやり直し、差分を同じコミットに入れる（RULES.md「運用」/ SPEC_UE.md 7.3 と同じ扱い）。

それ以外で Fixtures/ の中身が変わるのは異常。照合テストが落ちたら、データを書き直して通すのではなく **`WebMock/src/core` かテストの側を疑う**。

## 照合テスト

| 版 | テスト |
| --- | --- |
| Web 版 | `WebMock/tests/fixtures.test.ts`（`npm --prefix WebMock test`）。`buildFixtureFiles()` で作り直した内容とコミット済みのファイルを、ファイル名の集合ごと比べる |
| UE 版 | `Source/CUBELITHCore/Private/Tests/` の Automation Test（U1 で作る。SPEC_UE.md 7.3） |
