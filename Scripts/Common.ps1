# Build.ps1 / Test.ps1 が共通で使うパス。直接は実行しない（dot source で読み込む）

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProject = Join-Path $ProjectRoot 'CUBELITH.uproject'

# エンジンの場所: 環境変数 UE_ROOT があればそれを使い、無ければ .uproject の EngineAssociation（"5.8"）から
# Launcher 版の登録（HKLM\SOFTWARE\EpicGames\Unreal Engine\<版>）を引く
function Get-EngineRoot {
    if ($env:UE_ROOT) { return $env:UE_ROOT }
    $association = (Get-Content -LiteralPath $UProject -Raw | ConvertFrom-Json).EngineAssociation
    $key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association"
    $dir = (Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue).InstalledDirectory
    if (-not $dir) { throw "UE $association の場所が分からない。環境変数 UE_ROOT にエンジンのフォルダを設定する" }
    return $dir
}

$EngineRoot = Get-EngineRoot
$BuildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
