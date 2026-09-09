# 8.163：訳の表を作り直す／突き合わせる（Phase 201）
#
#   .\Tools\Update-Translations.ps1            … 突き合わせるだけ（何も書き換えない）
#   .\Tools\Update-Translations.ps1 -Write     … Source/LocaliseTable.inc を作り直す
#
# **なぜ要るか。**
# 訳の見出しは「ソースに書いてある日本語そのもの」です（Localise.h）。
# 見出しを別に付けない代わりに、**日本語を書き換えたら表も書き換える**必要があります。
# 書き換え忘れは画面を壊しませんが、**そこだけ日本語のまま残る**という
# 見つけにくい形で出るので、機械に数えさせます。
#
# 出るもの：
#   MISSING … ソースにあるのに表に無い（訳されないまま出る）
#   STALE   … 表にあるのにソースに無い（消していい行）
#
# -Write のときは、いまの .inc の訳を引き継いだまま並べ直します。
# **MISSING の英語は空のまま**入るので、そこを埋めてください
# （空の訳は Localise.cpp が「訳なし」と同じに扱い、日本語を出します）。

param([switch]$Write)

$ErrorActionPreference = 'Stop'

$root   = Split-Path -Parent $PSScriptRoot
$src    = Join-Path $root 'Source'
$incPath = Join-Path $src 'LocaliseTable.inc'

# --- ソースから抜く（連結された文字列リテラルもひとつに繋ぐ） -------------------
$rx  = [regex]'utf8\s*\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)'
$lit = [regex]'"((?:[^"\\]|\\.)*)"'

$fromSource = [ordered]@{}

Get-ChildItem $src -Include *.cpp,*.h -Recurse | Sort-Object Name | ForEach-Object {
    $file = $_.Name
    $text = [System.IO.File]::ReadAllText($_.FullName, [System.Text.Encoding]::UTF8)

    foreach ($m in $rx.Matches($text))
    {
        $s = ''
        foreach ($l in $lit.Matches($m.Groups[1].Value)) { $s += $l.Groups[1].Value }
        if ($s.Length -gt 0 -and -not $fromSource.Contains($s)) { $fromSource[$s] = $file }
    }
}

# --- いまの表を読む ------------------------------------------------------------
$existing = @{}

if (Test-Path $incPath)
{
    $entryRx = [regex]'^\s*\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*(true|false)\s*\},\s*$'

    foreach ($line in [System.IO.File]::ReadAllLines($incPath, [System.Text.Encoding]::UTF8))
    {
        $m = $entryRx.Match($line)
        if ($m.Success) { $existing[$m.Groups[1].Value] = @($m.Groups[2].Value, $m.Groups[3].Value) }
    }
}

# --- 突き合わせ ----------------------------------------------------------------
$missing = @($fromSource.Keys | Where-Object { -not $existing.ContainsKey($_) })
$stale   = @($existing.Keys   | Where-Object { -not $fromSource.Contains($_) })

Write-Output "source : $($fromSource.Count) strings"
Write-Output "table  : $($existing.Count) entries"
Write-Output "MISSING: $($missing.Count)"
foreach ($k in $missing) { Write-Output "  + [$($fromSource[$k])] $k" }
Write-Output "STALE  : $($stale.Count)"
foreach ($k in $stale) { Write-Output "  - $k" }

if (-not $Write) { return }

# --- 書き直す ------------------------------------------------------------------
$sb = New-Object System.Text.StringBuilder
$lastFile = ''

foreach ($k in $fromSource.Keys)
{
    $file = $fromSource[$k]

    if ($file -ne $lastFile)
    {
        [void]$sb.AppendLine('')
        [void]$sb.AppendLine("    // $file")
        $lastFile = $file
    }

    $en   = if ($existing.ContainsKey($k)) { $existing[$k][0] } else { '' }
    $flag = if ($existing.ContainsKey($k)) { $existing[$k][1] } else { 'false' }

    [void]$sb.AppendLine("    { `"$k`", `"$en`", $flag },")
}

[System.IO.File]::WriteAllText($incPath, $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))
Write-Output "wrote $incPath"
