// scripts/ で使う Node の組み込みモジュールの型宣言。
//
// 解釈: WebMock の依存に `@types/node` は入っておらず、このタスクではネットワークに出ない制約から
// 新しく取得しない。それでも `npm run build` の `tsc --noEmit` を通す必要があるので、実際に使う
// 関数だけをここで最小限に宣言する（`@types/node` を入れたときはこのファイルを消す）。

declare module 'node:fs' {
  /** ディレクトリを作る。recursive なら途中のディレクトリも作り、既にあってもエラーにしない。 */
  export function mkdirSync(path: string, options: { readonly recursive: true }): string | undefined;
  /** ファイルを書き出す（既にあれば上書き）。 */
  export function writeFileSync(path: string, data: string, encoding: 'utf8'): void;
}

declare module 'node:url' {
  /** file: URL をこのプラットフォームのパスに変換する。 */
  export function fileURLToPath(url: URL | string): string;
}
