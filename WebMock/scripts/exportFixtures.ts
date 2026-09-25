// UE 版と照合するデータ（golden fixtures）の書き出し（SPEC_UE.md 7.3）。
// `npm run export:fixtures`（ルートからは `npm --prefix WebMock run export:fixtures`）で実行する。
// 組み立ては scripts/fixtures/build.ts が行い、ここは fs への書き出しだけを担う。

import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { buildFixtureFiles, stringifyFixture } from './fixtures/build';

/** 出力先。UE 版のテストが読む場所（SPEC_UE.md 7.3）。 */
// スクリプト自身の位置から解決するので、どのディレクトリから実行しても同じ場所へ書き出す
// （process.cwd() に依存しない）。
const OUTPUT_DIR = new URL('../../Source/CUBELITHCore/Private/Tests/Fixtures/', import.meta.url);

const files = buildFixtureFiles();

mkdirSync(fileURLToPath(OUTPUT_DIR), { recursive: true });
for (const file of files) {
  // 既にあるファイルは上書きするだけで、消しはしない
  //（不要になったファイルの検出は index.json の files と実ファイルを比べるテスト側で行う）
  writeFileSync(fileURLToPath(new URL(file.path, OUTPUT_DIR)), stringifyFixture(file), 'utf8');
}

// 実行ごとに変わる値は出力に混ぜない（2 回実行して内容が 1 バイトも変わらないことが達成条件）
console.log(`照合データを書き出した: ${files.length} ファイル`);
