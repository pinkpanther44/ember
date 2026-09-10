<#
    8.176：**配る形にまとめる**（Phase 217）。

        pwsh Tools/Package-Ember.ps1

    これ1本で、

      1. Ember を Release でビルドし直し
      2. `out-ember/package/Ember-<version>-Windows-x64/` に中身をそろえ
      3. それを zip に
      4. Inno Setup が入っていれば、インストーラーも

    を行います。**zip だけでも配れます**——インストーラーが無くても止まりません。

    ─────────────────────────────────────────────────────────────────
    zip に入れるもの
    ─────────────────────────────────────────────────────────────────

      Ember.exe        ← Visual C++ ランタイム込み（/MT。8.176）
      README.md
      CHANGELOG.md
      LICENSE.txt

    **これだけです。** DLLを並べていないのは、そのほうが
    「解凍して起動」が確実だからです（足りないDLLで起動しない、が起きない）。

    ─────────────────────────────────────────────────────────────────
    署名について
    ─────────────────────────────────────────────────────────────────

    **署名していません。** SmartScreenが「発行元不明」と出しますが、
    個人開発のDAWやプラグインではよくあることで、README に回避手順を書いてあります。
    証明書を買ったら、ここに `signtool` を1行足してください。
#>

[CmdletBinding()]
param(
    [string] $Configuration = 'Release',

    # ビルドを飛ばして、いまある成果物をそのまま包む
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'

$repo      = Split-Path -Parent $PSScriptRoot
$buildDir  = Join-Path $repo 'out-ember'
$artefacts = Join-Path $buildDir "PersonalDAW_artefacts\$Configuration"
$exe       = Join-Path $artefacts 'Ember.exe'
$assets    = Join-Path $repo 'Packaging\Ember'

# バージョンは CMakeLists.txt の project(... VERSION x.y.z) から読みます。
# **2か所に書かないこと**——片方だけ上げたときに、zipの名前と
# アプリのバージョンが食い違います
$version = (Select-String -Path (Join-Path $repo 'CMakeLists.txt') `
                          -Pattern 'project\s*\(\s*PersonalDAW\s+VERSION\s+([0-9.]+)').Matches[0].Groups[1].Value

if (-not $version) { throw "Could not read the version from CMakeLists.txt" }

Write-Host "Ember $version ($Configuration)" -ForegroundColor Cyan

#--------------------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "`nBuilding..." -ForegroundColor Cyan

    # **-DMANTA_BRAND=ember を毎回渡します。** キャッシュに残っているはずですが、
    # 消してしまった人が Manta Studio を包んでしまうと気づきにくいので
    cmake -S $repo -B $buildDir -DMANTA_BRAND=ember | Out-Null
    cmake --build $buildDir --config $Configuration --target PersonalDAW | Out-Null

    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

if (-not (Test-Path $exe)) { throw "Not found: $exe" }

#--------------------------------------------------------------------------
# **配る前に、これがEmberであることを確かめます。**
# ブランドを間違えて包むと、中身がManta Studioのまま配られます
$exeBytes = [System.IO.File]::ReadAllBytes($exe)
$exeText  = [System.Text.Encoding]::Unicode.GetString($exeBytes)

if ($exeText -notmatch 'Ember') {
    throw "$exe does not look like an Ember build. Configure with -DMANTA_BRAND=ember."
}

# 8.191：**exeのプロパティも見ること**（Phase 229／実際に踏みました）。
#
# `CMakeLists.txt`の`COMPANY_NAME`を直したのに、**Ember側だけ"YourName"のまま**でした。
# 版情報（`PersonalDAW_resources.rc`）は**juceaideがビルド時に1度作って、
# それ以降は作り直しません**——`CMakeLists.txt`は、その生成の依存に入っていないためです。
# 8.165のアイコンと**同じ種類の取りこぼし**で、ビルドは成功するので気づけません。
#
# 直すには、**`.rc`を消してからビルドし直します**。
# ここで止めておけば、間違った作者名のまま配ることはありません。
$info = (Get-Item $exe).VersionInfo

if ($info.CompanyName -ne 'pinkpanther44' -or $info.ProductName -ne 'Ember') {
    Write-Host ""
    Write-Host ("  exe says   : Company='{0}' Product='{1}'" -f $info.CompanyName, $info.ProductName) -ForegroundColor Yellow
    Write-Host  "  expected   : Company='pinkpanther44' Product='Ember'" -ForegroundColor Yellow
    Write-Host  "  Delete this file and build again (juceaide only writes it once):" -ForegroundColor Yellow
    Write-Host ("    $buildDir\PersonalDAW_artefacts\JuceLibraryCode\PersonalDAW_resources.rc")
    throw "The executable's version info is stale."
}

# ランタイムを抱えているか（/MT。8.176）
$ascii = [System.Text.Encoding]::ASCII.GetString($exeBytes)

if ($ascii -match 'VCRUNTIME140') {
    Write-Warning "This build still imports VCRUNTIME140.dll - it will not start on a PC without the Visual C++ redistributable."
    Write-Warning "Re-configure so that CMAKE_MSVC_RUNTIME_LIBRARY takes effect (delete $buildDir and try again)."
}

#--------------------------------------------------------------------------
$name      = "Ember-$version-Windows-x64"
$packageDir = Join-Path $buildDir 'package'
$stage     = Join-Path $packageDir $name

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item $exe $stage
foreach ($file in @('README.md', 'CHANGELOG.md', 'LICENSE.txt')) {
    Copy-Item (Join-Path $assets $file) $stage
}

$zip = Join-Path $packageDir "$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip

Write-Host ("`n  zip       : $zip  ({0:N1} MB)" -f ((Get-Item $zip).Length / 1MB)) -ForegroundColor Green

#--------------------------------------------------------------------------
# インストーラー。**無ければ黙って飛ばします**（zipだけでも配れる）
# 8.191：**ユーザー領域も見ること**（Phase 229）。
# `winget install JRSoftware.InnoSetup` は管理者権限を要求しないので、
# **`%LOCALAPPDATA%\Programs` の下へ入ります**——Program Files しか見ていないと、
# 入れたのに「入っていません」と言われます
$iscc = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($iscc) {
    Write-Host "`nBuilding the installer..." -ForegroundColor Cyan

    & $iscc "/DAppVersion=$version" "/DStageDir=$stage" (Join-Path $PSScriptRoot 'Ember.iss') | Out-Null

    $setup = Join-Path $buildDir "installer\Ember-$version-Windows-x64-Setup.exe"

    if (Test-Path $setup) {
        Write-Host ("  installer : $setup  ({0:N1} MB)" -f ((Get-Item $setup).Length / 1MB)) -ForegroundColor Green
    }
}
else {
    Write-Host "`n  installer : skipped (Inno Setup 6 is not installed)" -ForegroundColor Yellow
    Write-Host "              https://jrsoftware.org/isinfo.php"
}

Write-Host ""
Write-Host "Next: attach these to a GitHub release."
