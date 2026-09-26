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

# -DisableAdaptiveUnity: 作業中のファイルを unity build から外す動き（adaptive unity）を止め、常にまとめてコンパイルする。
# 外したままだと、別の .cpp の無名名前空間の同じ名前がぶつかる誤りを verify が見逃し、コミット後のビルドで初めて落ちる
& $BuildBat CUBELITHEditor Win64 $Configuration "-Project=$UProject" -WaitMutex -NoHotReloadFromIDE -DisableAdaptiveUnity
exit $LASTEXITCODE
