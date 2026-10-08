param(
    [string]$Repo   = "lmg325586/PopKiller",
    [string]$Branch = "master",
    [int]$WaitSeconds = 4,
    [switch]$NoPush,
    [switch]$Check
)
$ErrorActionPreference = "Stop"
if ($PSVersionTable.PSEdition -ne 'Core' -or $PSVersionTable.PSVersion.Major -lt 7) {
    throw "This script requires PowerShell 7 (pwsh). Run it with: pwsh -File `"$PSCommandPath`""
}

$jsonName = "community_rules.json"
$shaName  = "community_rules_sha256"
$base     = "https://raw.githubusercontent.com/$Repo/$Branch"
$tmp      = Join-Path $env:TEMP "cr_raw_$PID.json"

git rev-parse --show-toplevel | Out-Null
if ($LASTEXITCODE -ne 0) { throw "请在 git 仓库根目录运行本脚本。" }

if ($WaitSeconds -gt 0) {
    Write-Host "等待 ${WaitSeconds}s 让 GitHub 部署到 raw ..."
    Start-Sleep -Seconds $WaitSeconds
}

$tick = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
Invoke-WebRequest -Uri "$base/${jsonName}?t=$tick" -OutFile $tmp
$rawHash = (Get-FileHash $tmp -Algorithm SHA256).Hash.ToLower()

$localHash = (Get-FileHash $jsonName -Algorithm SHA256).Hash.ToLower()
if ($localHash -ne $rawHash) {
    Write-Warning "本地字节与 raw 字节不一致（sha 以 raw 为准）："
    Write-Warning "  local = $localHash"
    Write-Warning "  raw   = $rawHash"
}

if ($Check) {
    $cur = (Get-Content $shaName -Raw)
    $pos = $cur.IndexOf(':')
    $curHex = if ($pos -ge 0) { $cur.Substring($pos + 1) } else { $cur }
    $curHex = $curHex.Trim().ToLower()
    Remove-Item $tmp -ErrorAction SilentlyContinue
    if ($curHex -ne $rawHash) {
        Write-Error "sha 文件($curHex) 与 raw($rawHash) 不匹配。"
        exit 1
    }
    Write-Host "OK: sha 文件与 raw 字节匹配。"
    exit 0
}

"SHA256:$rawHash" | Set-Content -NoNewline -Encoding ascii $shaName
Write-Host "已更新 $shaName -> SHA256:$rawHash"

if (-not $NoPush) {
    git add $shaName
    $st = git status --porcelain -- $shaName
    if ($st) {
        git commit -m "chore: update community_rules sha256 from raw bytes"
        git push
        Write-Host "已提交并 push sha 更新。"
    } else {
        Write-Host "sha 无变化，跳过提交。"
    }
}
Remove-Item $tmp -ErrorAction SilentlyContinue