$root = Split-Path -Parent $PSScriptRoot

$gitHash = "unknown"
try { $gitHash = (git rev-parse --short HEAD) 2>$null } catch {}
if (-not $gitHash) { $gitHash = "unknown" }
$date = Get-Date -Format "yyyyMMdd"

$baseVersion = "0.8"
# Edition suffix for the lite branch (built from code points to keep this script ASCII)
$edition = ""
try { $branch = (git rev-parse --abbrev-ref HEAD) 2>$null } catch {}
if ($branch -eq "lite-en") { $edition = " " + [char]0x7CBE + [char]0x7B80 + [char]0x7248 }
$displayString = "Beta $baseVersion$edition ($date.$gitHash)"
$headerPath = Join-Path $root "winui\VersionInfo.h"
$content = "#pragma once`r`n#define APP_VERSION_STRING L`"$displayString`"`r`n"

$need = $true
if (Test-Path $headerPath) {
    $old = Get-Content $headerPath -Raw
    if ($old -and $old.Contains($displayString)) { $need = $false }
}
if ($need) {
    # UTF-8 with BOM so MSVC (ACP 936) reads the edition text correctly
    $utf8bom = New-Object System.Text.UTF8Encoding($true)
    [System.IO.File]::WriteAllText($headerPath, $content, $utf8bom)
    Write-Host "VersionInfo.h -> $displayString"
} else {
    Write-Host "VersionInfo.h up-to-date"
}

$rcPath = Join-Path $root "winui\winui.rc"
if (Test-Path $rcPath) {
    $rc = [System.IO.File]::ReadAllText($rcPath)
    if (-not $rc.Contains($displayString)) {
        $verStr = "$baseVersion.$date.$gitHash"
        $verParts = $baseVersion -split '\.'
        $verQuad = "0,$($verParts[1]),0,0"
        $rc = [regex]::Replace($rc, 'FILEVERSION\s+[\d,]+', "FILEVERSION $verQuad")
        $rc = [regex]::Replace($rc, 'PRODUCTVERSION\s+[\d,]+', "PRODUCTVERSION $verQuad")
        $rc = [regex]::Replace($rc, '(?<=VALUE "FileVersion",\s*")[^"]*', $verStr)
        $rc = [regex]::Replace($rc, '(?<=VALUE "ProductVersion",\s*")[^"]*', $verStr)
        [System.IO.File]::WriteAllText($rcPath, $rc)
        Write-Host "winui.rc -> $verStr"
    }
}