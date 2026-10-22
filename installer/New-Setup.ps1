# New-Setup.ps1 — 从已构建的 Release 输出生成 PopKiller 安装包（Inno Setup / 机器级 / 中文界面）
#
# 前置：
#   1) 已构建：x64\Release\winui\winui.exe 存在；
#   2) 已安装 Inno Setup 6.5+（含 ISCC.exe），并确保 installer\lang\ChineseSimplified.isl 存在。
#
# 用法（在仓库根或任意位置）：
#   pwsh -File installer\New-Setup.ps1
#   pwsh -File installer\New-Setup.ps1 -Version 0.10.1 -SetupName PopKiller-Beta0.10.1-x64-setup
#
# 说明：脚本把 VC++ 运行时（app-local）与打包所需文件放进临时舞台，排除 pdb/lib/exp 与运行期文件，
#       再调用 ISCC 编译 installer\PopKiller.iss。产物默认输出到 dist\。

[CmdletBinding()]
param(
  [string]$AppOutput,                                   # 默认 <repo>\x64\Release\winui
  [string]$OutputDir,                                   # 默认 <repo>\dist
  [string]$Version = "0.10.1",
  [string]$SetupName,                                   # 不含扩展名的输出文件名
  [string]$Iscc                                         # 可选：显式指定 ISCC.exe 路径
)

$ErrorActionPreference = "Stop"
if ($PSVersionTable.PSEdition -ne 'Core' -or $PSVersionTable.PSVersion.Major -lt 7) {
    throw "This script requires PowerShell 7 (pwsh). Run it with: pwsh -File `"$PSCommandPath`""
}

$root = Split-Path -Parent $PSScriptRoot
if (-not $AppOutput) { $AppOutput = Join-Path $root "x64\Release\winui" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "dist" }
if (-not $SetupName) { $SetupName = "PopKiller-Beta$Version-x64-setup" }
$issPath = Join-Path $PSScriptRoot "PopKiller.iss"
$outExe = Join-Path $OutputDir "$SetupName.exe"

if (-not (Test-Path (Join-Path $AppOutput "winui.exe"))) {
    throw "找不到 $AppOutput\winui.exe，请先构建 Release|x64。"
}
if (-not (Test-Path (Join-Path $PSScriptRoot "lang\ChineseSimplified.isl"))) {
    throw "缺少 installer\lang\ChineseSimplified.isl（简体中文语言文件）"
}

# ISCC（Inno Setup 编译器，支持 6.x / 7.x）
if (-not $Iscc) {
    $candidates = @()
    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, (Join-Path $env:LOCALAPPDATA "Programs"))) {
        if ($base -and (Test-Path $base)) {
            $candidates += Get-ChildItem $base -Directory -Filter "Inno Setup*" -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending |
                ForEach-Object { Join-Path $_.FullName "ISCC.exe" } |
                Where-Object { Test-Path $_ }
        }
    }
    $Iscc = $candidates | Select-Object -First 1
    if (-not $Iscc) { $Iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source }
}
if (-not $Iscc -or -not (Test-Path $Iscc)) {
    throw "未找到 ISCC.exe，请先安装 Inno Setup 6.5+（https://jrsoftware.org/isdl.php）"
}

# 舞台（临时目录，避免污染构建输出）
$stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("popkiller_setup_" + [guid]::NewGuid().ToString("N"))
$stage = Join-Path $stagingRoot "PopKiller"
New-Item -ItemType Directory -Force -Path $stage | Out-Null

try {
    Write-Host "复制输出到舞台：$stage"
    Copy-Item (Join-Path $AppOutput '*') $stage -Recurse -Force

    Write-Host "排除 pdb/lib/exp 与运行期文件"
    Get-ChildItem $stage -Recurse -File | Where-Object { $_.Extension -in @('.pdb','.lib','.exp') } | Remove-Item -Force
    Get-ChildItem $stage -Force -File |
        Where-Object { $_.Name -in @('rules.json','labels.json','winui.ini','blocklog.txt','autostart_debug.log','notifydiag.txt') -or $_.Extension -eq '.log' } |
        Remove-Item -Force

    # VC++ 运行时（app-local）
    $redistRoot = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Redist\MSVC" -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($redistRoot) {
        $crt = Get-ChildItem $redistRoot.FullName -Recurse -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' -and $_.FullName -notmatch 'onecore' } | Select-Object -First 1
        if ($crt) { Copy-Item (Join-Path $crt.FullName '*.dll') $stage -Force; Write-Host "已加入 VC++ 运行时：$($crt.FullName)" }
        else { Write-Warning "未找到 x64 VC++ CRT，目标机需自备 VC++ 运行库" }
    }
    else { Write-Warning "未找到 VS Redist 目录，目标机需自备 VC++ 运行库" }

    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    if (Test-Path $outExe) { Remove-Item $outExe -Force }

    Write-Host "编译安装包：$outExe"
    & $Iscc "/DStageDir=$stage" "/DAppVer=$Version" "/DOutDir=$OutputDir" "/DSetupName=$SetupName" $issPath
    if ($LASTEXITCODE -ne 0) { throw "ISCC 编译失败（exit $LASTEXITCODE）" }
    if (-not (Test-Path $outExe)) { throw "未生成 $outExe" }

    $files = (Get-ChildItem $stage -Recurse -File | Measure-Object).Count
    $f = Get-Item $outExe
    Write-Host ("完成：{0}  ({1:N2} MB, {2} 个文件)" -f $f.Name, ($f.Length / 1MB), $files)
}
finally {
    Remove-Item -Recurse -Force $stagingRoot -ErrorAction SilentlyContinue
}
