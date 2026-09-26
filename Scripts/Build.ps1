#Requires -Version 7.0
<#
  UE 版のエディタ用ターゲット（CUBELITHEditor）をコマンドラインでビルドする。Auto_Tasks の verify にも使う
  使い方（リポジトリルートから）: pwsh -NoProfile -File Scripts/Build.ps1
  エディタを開いたまま（Live Coding が有効）だと失敗する。エディタを閉じてから実行する
#>
param(
    [ValidateSet('Development', 'DebugGame')][string]$Configuration = 'Development'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

& $BuildBat CUBELITHEditor Win64 $Configuration "-Project=$UProject" -WaitMutex -NoHotReloadFromIDE
exit $LASTEXITCODE
