// tests/ で使う Node の組み込みモジュールの型宣言。
//
// 解釈: WebMock の依存に `@types/node` は入っておらず（`scripts/node-builtins.d.ts` と同じ事情）、
// このタスクでも新しく取得しない。`scripts/` 側は書き出しに使う関数だけを宣言しているので、
// 照合データのテストで使う読み取りの関数をここで足す（同じ 'node:fs' への宣言はマージされる。
// `@types/node` を入れたときはこのファイルと `scripts/node-builtins.d.ts` を消す）。

declare module 'node:fs' {
  /** ディレクトリ直下の名前の一覧。並びは環境依存なので、比べる前に呼び出し側で並べ替える。 */
  export function readdirSync(path: URL | string): string[];
  /** ファイルを文字列として読む。 */
  export function readFileSync(path: URL | string, encoding: 'utf8'): string;
}
