<#
.SYNOPSIS
    テンポと拍子の変化点（Source/TempoMap.h）の値を、アプリを起動せずに確かめる。

.DESCRIPTION
    `Source/TempoMap.h` は「小節 -> 拍 -> 秒」の換算だけを持つ、純粋な計算です
    （Phase 140／HANDOVER 8.102）。**間違っていても画面では「なんとなくずれている」
    としか見えない**ので、数値で押さえておく道具です（8.4）。

    juce_core とSTLしか使っていないので、`Tools/ChordEngineTests/juce_core` の
    最小スタブをインクルードパスの先頭に置くだけで、JUCEをビルドせずに走ります。
    **スタブは共有しています**（同じものを2つ置くと必ず片方が古くなる。8.55）。

    **テンポマップの計算を触ったら、必ずこれを走らせてから次へ進んでください。**

.PARAMETER KeepBinaries
    生成した .exe / .obj を消さずに残します。

.EXAMPLE
    .\Run-TempoMapTests.ps1
#>

[CmdletBinding()]
param(
    [switch] $KeepBinaries
)

$ErrorActionPreference = 'Stop'

$testDir = $PSScriptRoot
$repoRoot = Split-Path (Split-Path $testDir -Parent) -Parent
$sourceDir = Join-Path $repoRoot 'Source'
$stubDir = Join-Path $repoRoot 'Tools\ChordEngineTests'

if (-not (Test-Path (Join-Path $sourceDir 'TempoMap.h'))) {
    throw "Source/TempoMap.h が見つかりません（探した場所: $sourceDir）"
}

if (-not (Test-Path (Join-Path $stubDir 'juce_core\juce_core.h'))) {
    throw "juce_core の最小スタブが見つかりません（探した場所: $stubDir）"
}

$outDir = Join-Path $env:TEMP 'PersonalDAW-TempoMapTests'
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

# ---- MSVCを探す -------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe が見つかりません。Visual Studio（C++ワークロード）が要ります: $vswhere"
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw 'C++ツールセットを持つVisual Studioが見つかりません。'
}

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat が見つかりません: $vcvars"
}

Write-Host "Visual Studio : $vsPath"
Write-Host "Source        : $sourceDir"
Write-Host ''

function Invoke-WithMsvc {
    param([string] $CommandLine)

    $full = '"' + $vcvars + '" >nul 2>nul && chcp 65001 >nul && ' + $CommandLine

    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & cmd.exe /c $full | Out-Host
    }
    finally {
        $ErrorActionPreference = $saved
    }

    return $LASTEXITCODE
}

$exe = Join-Path $outDir 'test_tempomap.exe'

# /I の順番が肝心：スタブの juce_core を、本物のJUCEより先に見せる
$compile = 'cl /nologo /std:c++20 /utf-8 /EHsc /W4' `
    + ' /I "' + $stubDir + '"' `
    + ' /I "' + $sourceDir + '"' `
    + ' /Fo:"' + $outDir + '\\"' `
    + ' "' + (Join-Path $testDir 'test_tempomap.cpp') + '"' `
    + ' /Fe:"' + $exe + '"'

$code = Invoke-WithMsvc $compile
if ($code -ne 0) {
    Write-Host "コンパイルに失敗しました（終了コード $code）" -ForegroundColor Red
    exit 1
}

$code = Invoke-WithMsvc ('"' + $exe + '"')

if (-not $KeepBinaries) {
    Remove-Item (Join-Path $outDir '*') -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
if ($code -eq 0) {
    Write-Host 'TempoMap: OK' -ForegroundColor Green
    exit 0
}

Write-Host 'TempoMap: 失敗あり' -ForegroundColor Red
exit 1
