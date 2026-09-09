<#
.SYNOPSIS
    コード進行支援エンジン（仕様書5.11）の値を、アプリを起動せずに確かめる。

.DESCRIPTION
    `Source/ChordModel.h` と `Source/ChordEngine.h` は「画面にも音にも出ない」ため、
    移植・改修しても壊れたことに気づけません（HANDOVER 8.5）。
    この道具は、その2ファイルだけをその場でコンパイルして走らせ、
    コードの構成音・候補グリッド・スコア・ボイシング・ノート生成の値が
    期待どおりかを一覧します。

    エンジンが依存しているのは juce_core とSTLだけなので、
    このフォルダの `juce_core/juce_core.h`（最小スタブ）をインクルードパスの先頭に
    置くだけで、JUCEを一切ビルドせずに動きます。数秒で終わります。

    最後に `compile_check.cpp` を**本物のJUCEヘッダ**でコンパイル（リンクなし）して、
    スタブでしか通らないコードになっていないかも確かめます。

    **重みや音域を変えたら、必ずこれを走らせてから次へ進んでください。**
    期待値そのものを変えたい場合は、各 test_*.cpp の期待値を先に直します。

.PARAMETER JuceModules
    本物のJUCEのmodulesフォルダ。既定はCMakeがFetchContentで取ってきた
    `out/_deps/juce-src/modules`。無ければ最後のコンパイル確認だけ飛ばします。

.PARAMETER KeepBinaries
    生成した .exe / .obj を消さずに残します（デバッガで追いたいとき用）。

.EXAMPLE
    .\Run-ChordTests.ps1

    3つのテストを走らせて、失敗があれば終了コード1で返します。
#>

[CmdletBinding()]
param(
    [string] $JuceModules,

    [switch] $KeepBinaries
)

$ErrorActionPreference = 'Stop'

# ---- 場所を決める -----------------------------------------------------------
$testDir = $PSScriptRoot
$repoRoot = Split-Path (Split-Path $testDir -Parent) -Parent
$sourceDir = Join-Path $repoRoot 'Source'

if (-not (Test-Path (Join-Path $sourceDir 'ChordEngine.h'))) {
    throw "Source/ChordEngine.h が見つかりません（探した場所: $sourceDir）"
}

if (-not $JuceModules) {
    $JuceModules = Join-Path $repoRoot 'out\_deps\juce-src\modules'
}

$outDir = Join-Path $env:TEMP 'PersonalDAW-ChordTests'
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

# ---- MSVCを探す -------------------------------------------------------------
# cl.exe はPATHに居ないのが普通なので、vcvars64.bat を通してから呼ぶ。
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
Write-Host "作業フォルダ  : $outDir"
Write-Host ''

# cmd経由で vcvars を通してからコマンドを実行する。
# chcp 65001 は、テストが印刷する日本語を化けさせないため。
function Invoke-WithMsvc {
    param([string] $CommandLine)

    # vcvars64.bat は環境によっては vswhere が見つからない旨をstderrへ出す（実害なし）。
    # cmd側で捨てておかないと、Windows PowerShell 5.1 がそれをエラー扱いにして
    # $ErrorActionPreference='Stop' で止まってしまう。
    $full = '"' + $vcvars + '" >nul 2>nul && chcp 65001 >nul && ' + $CommandLine

    # cl.exe の警告もstderrに出ることがあるので、この間だけ止めない設定にする。
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        # Out-Host が要る。そのまま流すと、cl.exe や テストの出力そのものが
        # この関数の戻り値に混ざって、終了コードが読めなくなる。
        & cmd.exe /c $full | Out-Host
    }
    finally {
        $ErrorActionPreference = $saved
    }

    return $LASTEXITCODE
}

# ---- テストを1つ走らせる ----------------------------------------------------
$results = @()

function Invoke-ChordTest {
    param([string] $SourceFile)

    $name = [System.IO.Path]::GetFileNameWithoutExtension($SourceFile)
    $exe = Join-Path $outDir ($name + '.exe')

    Write-Host ('=' * 70)
    Write-Host $name
    Write-Host ('=' * 70)

    # /I の順番が肝心：スタブの juce_core を、本物のJUCEより先に見せる
    $compile = 'cl /nologo /std:c++20 /utf-8 /EHsc /W4' `
        + ' /I "' + $testDir + '"' `
        + ' /I "' + $sourceDir + '"' `
        + ' /Fo:"' + $outDir + '\\"' `
        + ' "' + (Join-Path $testDir $SourceFile) + '"' `
        + ' /Fe:"' + $exe + '"'

    $code = Invoke-WithMsvc $compile
    if ($code -ne 0) {
        Write-Host "コンパイルに失敗しました（終了コード $code）" -ForegroundColor Red
        return [pscustomobject]@{ Name = $name; Status = 'コンパイル失敗' }
    }

    $code = Invoke-WithMsvc ('"' + $exe + '"')
    Write-Host ''

    if ($code -eq 0) {
        return [pscustomobject]@{ Name = $name; Status = 'OK' }
    }
    return [pscustomobject]@{ Name = $name; Status = '失敗あり' }
}

foreach ($file in @('test_chordmodel.cpp', 'test_chordengine.cpp', 'test_voicing.cpp')) {
    $results += Invoke-ChordTest $file
}

# ---- 本物のJUCEでコンパイルできるかの確認 -----------------------------------
Write-Host ('=' * 70)
Write-Host 'compile_check（本物のJUCEヘッダ・リンクなし）'
Write-Host ('=' * 70)

if (-not (Test-Path $JuceModules)) {
    Write-Host "JUCEのmodulesが見つからないので飛ばします: $JuceModules" -ForegroundColor Yellow
    Write-Host '（先に cmake -B out を1度通してください）'
    $results += [pscustomobject]@{ Name = 'compile_check'; Status = '未実施' }
}
else {
    # ここではスタブを見せない（/I にテストフォルダを入れない）。
    # JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED は、JuceHeader.h 無しで
    # juce_core を直接includeするために要る。
    $compile = 'cl /nologo /c /std:c++20 /utf-8 /EHsc /W4' `
        + ' /DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 /DJUCE_STANDALONE_APPLICATION=1 /DDEBUG=1' `
        + ' /I "' + $sourceDir + '"' `
        + ' /I "' + $JuceModules + '"' `
        + ' /Fo:"' + $outDir + '\\"' `
        + ' "' + (Join-Path $testDir 'compile_check.cpp') + '"'

    $code = Invoke-WithMsvc $compile

    if ($code -eq 0) {
        Write-Host 'OK'
        $results += [pscustomobject]@{ Name = 'compile_check'; Status = 'OK' }
    }
    else {
        $results += [pscustomobject]@{ Name = 'compile_check'; Status = 'コンパイル失敗' }
    }
}

# ---- まとめ -----------------------------------------------------------------
Write-Host ''
Write-Host ('=' * 70)
$results | Format-Table -AutoSize

if (-not $KeepBinaries) {
    try { Remove-Item -Path (Join-Path $outDir '*') -Recurse -Force -ErrorAction Stop } catch {}
}

$failed = @($results | Where-Object { $_.Status -ne 'OK' -and $_.Status -ne '未実施' })
if ($failed.Count -gt 0) {
    Write-Host "$($failed.Count) 件が通っていません。" -ForegroundColor Red
    exit 1
}

Write-Host 'すべて通りました。' -ForegroundColor Green
exit 0
