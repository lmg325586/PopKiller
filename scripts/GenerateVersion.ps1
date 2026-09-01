$root = Split-Path -Parent $PSScriptRoot

$gitHash = "unknown"
try { $gitHash = (git rev-parse --short HEAD) 2>$null } catch {}
if (-not $gitHash) { $gitHash = "unknown" }
$date = Get-Date -Format "yyyyMMdd"

$baseVersion = "0.7"
$displayString = "Beta $baseVersion ($date.$gitHash)"
$headerPath = Join-Path $root "winui\VersionInfo.h"
$content = "#pragma once`r`n#define APP_VERSION_STRING L`"$displayString`"`r`n"

$need = $true
if (Test-Path $headerPath) {
    $old = Get-Content $headerPath -Raw
    if ($old -and $old.Contains($displayString)) { $need = $false }
}
if ($need) {
    [System.IO.File]::WriteAllText($headerPath, $content)
    Write-Host "VersionInfo.h -> $displayString"
} else {
    Write-Host "VersionInfo.h up-to-date"
}

$rcPath = Join-Path $root "winui\winui.rc"
if (Test-Path $rcPath) {
    $rc = [System.IO.File]::ReadAllText($rcPath)
    if (-not $rc.Contains($displayString)) {
        $verStr = "$baseVersion.$date.$gitHash"
        $rc = [regex]::Replace($rc, 'FILEVERSION\s+[\d,]+', 'FILEVERSION 0,5,0,0')
        $rc = [regex]::Replace($rc, 'PRODUCTVERSION\s+[\d,]+', 'PRODUCTVERSION 0,5,0,0')
        $rc = [regex]::Replace($rc, '(?<=VALUE "FileVersion",\s*")[^"]*', $verStr)
        $rc = [regex]::Replace($rc, '(?<=VALUE "ProductVersion",\s*")[^"]*', $verStr)
        [System.IO.File]::WriteAllText($rcPath, $rc)
        Write-Host "winui.rc -> $verStr"
    }
}