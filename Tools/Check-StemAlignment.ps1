<#
.SYNOPSIS
    書き出したWAVの「音の立ち上がり位置」をサンプル単位で報告する。

.DESCRIPTION
    仕様書5.7.1（PDC：プラグイン遅延補正）が効いているかを、耳ではなく数値で
    確かめるための道具です。

    PDCは「レイテンシを持つプラグインを一部のトラックだけに挿しても、
    トラック間の時間軸がずれない」ことを保証する仕組みです。ずれは数ミリ秒の
    ことが多く、聴いて判断するのは困難なので、ステム書き出し（仕様書5.10）した
    ファイルの立ち上がり位置を直接読んで比べます。

    使い方の全体像はHANDOVER.mdの「8.4 動作確認のやり方 → PDCのずれを数値で測る」
    を参照してください。

.PARAMETER Path
    調べるWAVファイル。フォルダを渡すと、その中の *.wav をまとめて調べます。

.PARAMETER ThresholdDb
    「音が始まった」とみなすレベル（dBFS）。既定は -40dB。
    ノイズフロアの高い素材では -30 などへ上げてください。

.EXAMPLE
    .\Check-StemAlignment.ps1 -Path "C:\Users\me\Desktop\stems"

    フォルダ内の全ステムの立ち上がり位置を一覧し、**ずれの最大値**を報告します。
    PDCが効いていれば、同じ位置に置いたクリップのステムは同じ値になります。
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Path,

    [double] $ThresholdDb = -40.0
)

function Read-WavOnset {
    param(
        [string] $File,
        [double] $ThresholdDb
    )

    $bytes = [System.IO.File]::ReadAllBytes($File)

    if ($bytes.Length -lt 44) { throw "ファイルが短すぎます: $File" }

    $riff = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4)
    $wave = [System.Text.Encoding]::ASCII.GetString($bytes, 8, 4)
    if ($riff -ne 'RIFF' -or $wave -ne 'WAVE') { throw "WAVファイルではありません: $File" }

    # チャンク（fmt / data 等）を順に辿る。WAVはチャンクの並び順が固定ではないため、
    # 決め打ちのオフセットで読まずに必ず走査すること。
    $pos = 12
    $fmtFound = $false
    $channels = 0; $sampleRate = 0; $bits = 0; $formatTag = 0
    $dataOffset = -1; $dataLength = 0

    while ($pos + 8 -le $bytes.Length) {
        $id = [System.Text.Encoding]::ASCII.GetString($bytes, $pos, 4)
        $size = [System.BitConverter]::ToUInt32($bytes, $pos + 4)
        $body = $pos + 8

        if ($id -eq 'fmt ') {
            $formatTag  = [System.BitConverter]::ToUInt16($bytes, $body)
            $channels   = [System.BitConverter]::ToUInt16($bytes, $body + 2)
            $sampleRate = [System.BitConverter]::ToUInt32($bytes, $body + 4)
            $bits       = [System.BitConverter]::ToUInt16($bytes, $body + 14)
            $fmtFound = $true

            # WAVE_FORMAT_EXTENSIBLE(0xFFFE) は実体をSubFormatのGUID先頭2バイトが持つ
            if ($formatTag -eq 0xFFFE -and $size -ge 40) {
                $formatTag = [System.BitConverter]::ToUInt16($bytes, $body + 24)
            }
        }
        elseif ($id -eq 'data') {
            $dataOffset = $body
            $dataLength = [Math]::Min([int64]$size, [int64]($bytes.Length - $body))
        }

        # チャンクは偶数境界に揃う（奇数サイズなら1バイトのパディングが入る）
        $pos = $body + $size + ($size % 2)
    }

    if (-not $fmtFound -or $dataOffset -lt 0) { throw "fmt/dataチャンクが見つかりません: $File" }

    $bytesPerSample = [int]($bits / 8)
    $frameBytes = $bytesPerSample * $channels
    if ($frameBytes -le 0) { throw "フォーマットを解釈できません: $File" }

    $totalFrames = [int]([Math]::Floor($dataLength / $frameBytes))
    $threshold = [Math]::Pow(10.0, $ThresholdDb / 20.0)

    $onsetFrame = -1
    $peak = 0.0

    for ($f = 0; $f -lt $totalFrames; $f++) {
        $base = $dataOffset + ($f * $frameBytes)

        for ($c = 0; $c -lt $channels; $c++) {
            $o = $base + ($c * $bytesPerSample)
            $v = 0.0

            switch ($bits) {
                16 { $v = [System.BitConverter]::ToInt16($bytes, $o) / 32768.0 }
                24 {
                    # 24bitリトルエンディアンを符号付き整数へ組み立てる
                    $raw = [int]$bytes[$o] -bor ([int]$bytes[$o + 1] -shl 8) -bor ([int]$bytes[$o + 2] -shl 16)
                    if ($raw -band 0x800000) { $raw = $raw - 0x1000000 }
                    $v = $raw / 8388608.0
                }
                32 {
                    if ($formatTag -eq 3) { $v = [System.BitConverter]::ToSingle($bytes, $o) }
                    else { $v = [System.BitConverter]::ToInt32($bytes, $o) / 2147483648.0 }
                }
                default { throw "$bits bit には未対応です: $File" }
            }

            $a = [Math]::Abs($v)
            if ($a -gt $peak) { $peak = $a }

            if ($onsetFrame -lt 0 -and $a -ge $threshold) { $onsetFrame = $f }
        }

        # 立ち上がりが見つかっても、ピーク（＝素材が本当に鳴っているかの確認）を
        # 取り切るために少しだけ読み進める
        if ($onsetFrame -ge 0 -and $f -gt $onsetFrame + $sampleRate) { break }
    }

    [pscustomobject]@{
        Name        = [System.IO.Path]::GetFileName($File)
        SampleRate  = $sampleRate
        Channels    = $channels
        Bits        = $bits
        Frames      = $totalFrames
        OnsetSample = $onsetFrame
        OnsetMs     = if ($onsetFrame -ge 0 -and $sampleRate -gt 0) { [Math]::Round($onsetFrame * 1000.0 / $sampleRate, 3) } else { $null }
        PeakDb      = if ($peak -gt 0) { [Math]::Round(20.0 * [Math]::Log10($peak), 1) } else { $null }
    }
}

$targets = @()

if (Test-Path -LiteralPath $Path -PathType Container) {
    $targets = Get-ChildItem -LiteralPath $Path -Filter *.wav | Sort-Object Name
} elseif (Test-Path -LiteralPath $Path) {
    $targets = @(Get-Item -LiteralPath $Path)
} else {
    throw "見つかりません: $Path"
}

if ($targets.Count -eq 0) { throw "WAVファイルがありません: $Path" }

$results = foreach ($t in $targets) { Read-WavOnset -File $t.FullName -ThresholdDb $ThresholdDb }

$results | Format-Table Name, SampleRate, Bits, Channels, OnsetSample, OnsetMs, PeakDb -AutoSize

$found = @($results | Where-Object { $_.OnsetSample -ge 0 })

if ($found.Count -lt 2) {
    Write-Host ""
    Write-Host "立ち上がりを検出できたファイルが1つ以下です。比較するには2つ以上必要です。" -ForegroundColor Yellow
    Write-Host "無音のステムがある場合は、そのトラックにクリップが置かれているか確認してください。" -ForegroundColor Yellow
    return
}

$min = ($found | Measure-Object -Property OnsetSample -Minimum).Minimum
$max = ($found | Measure-Object -Property OnsetSample -Maximum).Maximum
$spread = $max - $min
$rate = $found[0].SampleRate
$spreadMs = if ($rate -gt 0) { [Math]::Round($spread * 1000.0 / $rate, 3) } else { 0 }

Write-Host ""
Write-Host ("立ち上がり位置のずれ: {0} サンプル ({1} ms)" -f $spread, $spreadMs)

# 1サンプル程度のずれは、素材のフェードや検出しきい値の都合で普通に出る。
# PDCの有無で問題になるのは数十〜数千サンプル単位のずれ。
if ($spread -le 2) {
    Write-Host "→ 揃っています。PDCが効いているとみなせます。" -ForegroundColor Green
} else {
    Write-Host "→ ずれています。PDCが効いていない可能性があります。" -ForegroundColor Red
    Write-Host "   ミキサーのマスターに出ている PDC の値と、このずれの大きさを見比べてください。" -ForegroundColor Red
}
