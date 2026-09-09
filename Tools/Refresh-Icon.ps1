# 8.165：アイコンを差し替えたのに、ショートカットが古い絵のままのとき（Phase 203）
#
#   .\Tools\Refresh-Icon.ps1          … Windowsが覚えている絵を捨てさせる
#   .\Tools\Refresh-Icon.ps1 -Check   … いま何が入っているかを見るだけ
#
# **原因は2つあって、別物です。**
#
#   ① ビルドに入っていない
#      JUCEはexe用の`icon.ico`を**構成（configure）のときに1度だけ**作ります。
#      → Phase 203で`CMAKE_CONFIGURE_DEPENDS`に入れたので、
#        **`cmake --build`だけで入るようになりました**（もう起きません）。
#
#   ② Windowsが古い絵を覚えている
#      エクスプローラーはアイコンを**ファイルの場所ごとに覚えます**。
#      exeの場所は毎回同じなので、中の絵だけ変えても覚えたままのことがあります。
#      → これはビルドでは直せません。**このスクリプトの仕事**です。
#
# 何をしているか：
#   ie4uinit.exe -show     … アイコンのキャッシュを捨てさせる（**エクスプローラーは落としません**）
#   SHChangeNotify         … 「関連付けが変わった」と知らせて、絵を引き直させる
#
# それでも変わらないときは、**デスクトップでF5**を押してください。

param([switch]$Check)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot

function Show-IconOf($path, $label)
{
    if (-not (Test-Path $path)) { Write-Output "$label : (無し) $path"; return }

    try
    {
        $icon = [System.Drawing.Icon]::ExtractAssociatedIcon((Resolve-Path $path))
        $bmp  = $icon.ToBitmap()
        $tmp  = Join-Path $env:TEMP ("mantaicon_" + [guid]::NewGuid().ToString('N') + ".png")
        $bmp.Save($tmp, [System.Drawing.Imaging.ImageFormat]::Png)
        $hash = (Get-FileHash $tmp).Hash.Substring(0, 16)
        $bmp.Dispose(); $icon.Dispose(); Remove-Item $tmp -Force
        Write-Output "$label : $hash  ($path)"
    }
    catch { Write-Output "$label : (読めません) $path" }
}

# --- いま何が入っているか -------------------------------------------------------
Write-Output "--- exeに入っているアイコン（同じ値なら同じ絵） ---"
Show-IconOf (Join-Path $root 'out\PersonalDAW_artefacts\Debug\Manta Studio.exe')   'Debug  '
Show-IconOf (Join-Path $root 'out\PersonalDAW_artefacts\Release\Manta Studio.exe') 'Release'

$ico = Join-Path $root 'out\PersonalDAW_artefacts\JuceLibraryCode\icon.ico'

if (Test-Path $ico)
{
    $b = [System.IO.File]::ReadAllBytes($ico)
    $n = [BitConverter]::ToUInt16($b, 4)
    $sizes = for ($i = 0; $i -lt $n; $i++) { $o = 6 + $i * 16; $w = $b[$o]; if ($w -eq 0) { 256 } else { $w } }
    Write-Output "icon.ico: $($sizes -join ' / ') px"
}

if ($Check) { return }

# --- Windowsに覚え直させる ------------------------------------------------------
Write-Output ""
Write-Output "--- Windowsが覚えているアイコンを捨てさせます ---"

& "$env:SystemRoot\System32\ie4uinit.exe" -show
Start-Sleep -Milliseconds 500

Add-Type -Namespace Shell32 -Name Notify -MemberDefinition @'
[DllImport("shell32.dll")] public static extern void SHChangeNotify(int eventId, uint flags, System.IntPtr a, System.IntPtr b);
'@

# SHCNE_ASSOCCHANGED（関連付けが変わった）。エクスプローラーは落ちません
[Shell32.Notify]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)

Write-Output "済み。変わらないときは、デスクトップでF5を押してください。"
