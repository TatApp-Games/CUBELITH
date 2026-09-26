#Requires -Version 7.0
<#
  UE エディタで CUBELITH を開く
  使い方（リポジトリルートから）: pwsh -NoProfile -File Scripts/OpenEditor.ps1
  プロジェクトのパスを渡して起動するので、プロジェクト選択画面の「プロジェクトを変換」は出ない
  （このエンジンは自分を Launcher 版の "5.8" と認識できておらず、選択画面から開くと変換を求めてくる。変換はしない）
#>
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

Start-Process (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList "`"$UProject`""
