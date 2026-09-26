#Requires -Version 7.0
<#
  UE 版の Automation Test をコマンドラインで回す（先にビルドする）。Auto_Tasks の verify にも使う
  使い方（リポジトリルートから）: pwsh -NoProfile -File Scripts/Test.ps1 [-Filter CUBELITH.Core] [-NoBuild]
  - Filter はテスト名の前方一致（既定 CUBELITH = このプロジェクトのテスト全部）
  - 失敗が 1 件でもあるか、テストが 1 件も見つからなければ終了コード 1
  - エディタを開いたまま（Live Coding が有効）だとビルドで失敗する。エディタを閉じてから実行する
#>
param(
    [string]$Filter = 'CUBELITH',
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'Build.ps1')
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ビルドに失敗した（exit=$LASTEXITCODE）"
        exit $LASTEXITCODE
    }
}

$outDir = Join-Path $ProjectRoot 'Saved\Automation\CommandLine'
$reportDir = Join-Path $outDir 'Report'
$logPath = Join-Path $outDir 'Test.log'
if (Test-Path -LiteralPath $reportDir) { Remove-Item -LiteralPath $reportDir -Recurse -Force }
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

Write-Host "テストを実行: $Filter（ログ: $logPath）"
& $EditorCmd $UProject "-ExecCmds=Automation RunTests $Filter;Quit" '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$reportDir" "-abslog=$logPath" -unattended -nullrhi -nosplash -nosound -nopause | Out-Null
$editorExit = $LASTEXITCODE

$indexPath = Join-Path $reportDir 'index.json'
if (-not (Test-Path -LiteralPath $indexPath)) {
    Write-Host "テストの結果が出力されなかった（エディタの終了コード $editorExit）。ログを確認する: $logPath"
    Get-Content -LiteralPath $logPath -Tail 30 -ErrorAction SilentlyContinue
    exit 1
}

$report = Get-Content -LiteralPath $indexPath -Raw -Encoding UTF8 | ConvertFrom-Json
$passed = $report.succeeded + $report.succeededWithWarnings
Write-Host "成功 $passed / 失敗 $($report.failed) / 未実行 $($report.notRun)"

foreach ($test in $report.tests | Where-Object { $_.state -ne 'Success' }) {
    Write-Host "[$($test.state)] $($test.fullTestPath)"
    foreach ($entry in $test.entries | Where-Object { $_.event.type -eq 'Error' }) {
        Write-Host "    $($entry.event.message)"
    }
}

if ($report.failed -gt 0) { exit 1 }
if ($passed -eq 0) {
    Write-Host "テストが 1 件も見つからない（Filter: $Filter）"
    exit 1
}
exit 0
