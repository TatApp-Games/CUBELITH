#Requires -Version 7.0
<#
  UE エディタで CUBELITH を開く
  使い方（リポジトリルートから）: pwsh -NoProfile -File Scripts/OpenEditor.ps1
  Launcher やダブルクリックで開くのと同じ。コマンド 1 行で開きたいとき用
#>
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

Start-Process (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList "`"$UProject`""
