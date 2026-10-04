# New-Msi.ps1 — 从已构建的 Release 输出生成 PopKiller 的 MSI 安装包（机器级 / 中文界面）
#
# 前置：
#   1) 已构建：x64\Release\winui\winui.exe 存在；
#   2) 已安装 WiX v5（dotnet 工具）：dotnet tool install --global wix --version 5.0.2
#      （如为 v6/v7 需接受 OSMF EULA，建议用 v5）
#
# 用法（在仓库根或任意位置）：
#   pwsh -File installer\New-Msi.ps1
#   pwsh -File installer\New-Msi.ps1 -Version 0.9.1 -MsiName PopKiller-Beta0.9.1-x64.msi
#
# 说明：脚本会把 VC++ 运行时（app-local）与打包所需文件放进临时舞台，排除 pdb/lib/exp 与运行期文件，
#       生成 .wxs 后调用 wix build。产物默认输出到 dist\。

[CmdletBinding()]
param(
  [string]$AppOutput,                                   # 默认 <repo>\x64\Release\winui
  [string]$OutputDir,                                   # 默认 <repo>\dist
  [string]$Version = "0.9.0",
  [string]$MsiName,
  [string]$Culture = "zh-CN",
  [string]$UpgradeCode = "3F2E7C1A-9B44-4D2E-8C6F-1A2B3C4D5E6F"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $AppOutput)  { $AppOutput = Join-Path $root "x64\Release\winui" }
if (-not $OutputDir)  { $OutputDir = Join-Path $root "dist" }
if (-not $MsiName)    { $MsiName = "PopKiller-$Version-x64.msi" }
$msiPath = Join-Path $OutputDir $MsiName

function Esc([string]$s){ return $s.Replace('&','&amp;').Replace('<','&lt;').Replace('>','&gt;').Replace('"','&quot;') }
function DeterministicGuid([string]$s){
  $md5 = [System.Security.Cryptography.MD5]::Create()
  $b = $md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($s))
  $b[7] = [byte](($b[7] -band 0x0F) -bor 0x30)
  $b[8] = [byte](($b[8] -band 0x3F) -bor 0x80)
  return ([System.Guid]::new($b)).ToString().ToUpper()
}

if (-not (Test-Path (Join-Path $AppOutput "winui.exe"))) {
  throw "找不到 $AppOutput\winui.exe，请先构建 Release|x64。"
}

# WiX
$wix = Join-Path $env:USERPROFILE ".dotnet\tools\wix.exe"
if (-not (Test-Path $wix)) { $wix = (Get-Command wix.exe -ErrorAction SilentlyContinue).Source }
if (-not $wix) { throw "未找到 wix.exe，请先 dotnet tool install --global wix --version 5.0.2" }

# 舞台（临时目录，避免污染构建输出）
$stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("popkiller_msi_" + [guid]::NewGuid().ToString("N"))
$stage = Join-Path $stagingRoot "PopKiller"
New-Item -ItemType Directory -Force -Path $stage | Out-Null
$wxs = Join-Path $stagingRoot "PopKiller.wxs"

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
  } else { Write-Warning "未找到 VS Redist 目录，目标机需自备 VC++ 运行库" }

  # 卸载清理 CustomAction（删除遗留的自启项 HKCU/HKLM Run\PopKiller）
  $caProj = Join-Path $PSScriptRoot 'CleanupCA\CleanupCA.vcxproj'
  $caDll = Join-Path $PSScriptRoot 'CleanupCA\x64\Release\CleanupCA.dll'
  $msbuild = $null
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path $vswhere) { $msbuild = (& $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1) }
  if (-not $msbuild) { $msbuild = (Get-Command msbuild.exe -ErrorAction SilentlyContinue).Source }
  if ($msbuild) {
    Write-Host "构建卸载清理 CustomAction"
    & $msbuild $caProj /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "CleanupCA 构建失败" }
  } else { Write-Warning "未找到 MSBuild，跳过卸载清理 CustomAction" }
  $haveCa = Test-Path $caDll

  # 目录树 + 组件
  $dirEls = New-Object System.Collections.Generic.List[string]
  $comps  = New-Object System.Collections.Generic.List[string]
  $dirMap = @{}; $dirMap[$stage] = "INSTALLFOLDER"
  $script:stageLen = $stage.Length; $script:stageEsc = Esc $stage
  $script:dirMap = $dirMap; $script:dirEls = $dirEls; $script:comps = $comps; $script:dc = 0; $script:cc = 0

  function EmitDirs([string]$path, [int]$indent) {
    $pad = ' ' * $indent
    foreach ($d in (Get-ChildItem -LiteralPath $path -Directory | Sort-Object Name)) {
      $script:dc++; $id = "dir_{0:D4}" -f $script:dc
      $script:dirMap[$d.FullName] = $id
      $script:dirEls.Add("$pad<Directory Id=`"$id`" Name=`"$(Esc $d.Name)`">")
      EmitDirs $d.FullName ($indent + 2)
      $script:dirEls.Add("$pad</Directory>")
    }
  }
  function EmitComps([string]$path) {
    $dirId = $script:dirMap[$path]
    foreach ($f in (Get-ChildItem -LiteralPath $path -File | Sort-Object Name)) {
      $script:cc++; $cid = "cmp_{0:D4}" -f $script:cc; $fid = "fil_{0:D4}" -f $script:cc
      $rel = $f.FullName.Substring($script:stageLen).TrimStart('\')
      $script:comps.Add("      <Component Id=`"$cid`" Directory=`"$dirId`" Guid=`"$(DeterministicGuid $rel)`">")
      $script:comps.Add("        <File Id=`"$fid`" Source=`"$($script:stageEsc)\$rel`" KeyPath=`"yes`" />")
      $script:comps.Add("      </Component>")
    }
    foreach ($d in (Get-ChildItem -LiteralPath $path -Directory | Sort-Object Name)) { EmitComps $d.FullName }
  }
  EmitDirs $stage 6
  EmitComps $stage

  $sb = New-Object System.Text.StringBuilder
  [void]$sb.AppendLine('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs" xmlns:ui="http://wixtoolset.org/schemas/v4/wxs/ui">')
  [void]$sb.AppendLine("  <Package Name=`"PopKiller`" Manufacturer=`"lmg325586`" Version=`"$Version`" Language=`"2052`" UpgradeCode=`"$UpgradeCode`" Scope=`"perMachine`" Compressed=`"yes`">")
  [void]$sb.AppendLine('    <MajorUpgrade DowngradeErrorMessage="A newer version of PopKiller is already installed." />')
  [void]$sb.AppendLine('    <MediaTemplate EmbedCab="yes" />')
  if ($haveCa) {
    [void]$sb.AppendLine('    <Binary Id="CleanupCA" SourceFile="' + (Esc $caDll) + '" />')
    [void]$sb.AppendLine('    <CustomAction Id="DeleteAutostart" BinaryRef="CleanupCA" DllEntry="DeleteAutostart" Execute="deferred" Impersonate="yes" Return="ignore" />')
  }
  [void]$sb.AppendLine('    <Property Id="WIXUI_INSTALLDIR" Value="INSTALLFOLDER" />')
  [void]$sb.AppendLine('    <Property Id="DESKTOP_SHORTCUT" Value="1" />')
  [void]$sb.AppendLine('    <Property Id="ROOTDRIVE" Value="C:\" />')
  [void]$sb.AppendLine('    <StandardDirectory Id="TARGETDIR">')
  [void]$sb.AppendLine('      <Directory Id="INSTALLFOLDER" Name="PopKiller" />')
  [void]$sb.AppendLine('    </StandardDirectory>')
  [void]$sb.AppendLine('    <StandardDirectory Id="ProgramMenuFolder">')
  [void]$sb.AppendLine('      <Directory Id="ProgramMenuDir" Name="PopKiller" />')
  [void]$sb.AppendLine('    </StandardDirectory>')
  [void]$sb.AppendLine('    <StandardDirectory Id="DesktopFolder">')
  [void]$sb.AppendLine('      <Component Id="DesktopShortcut" Guid="8A7C1E20-1111-4A2B-9C3D-000000000003" Condition="DESKTOP_SHORTCUT">')
  [void]$sb.AppendLine('        <Shortcut Id="DesktopSC" Name="PopKiller" Target="[INSTALLFOLDER]winui.exe" WorkingDirectory="INSTALLFOLDER" />')
  [void]$sb.AppendLine('        <RegistryValue Root="HKMU" Key="Software\PopKiller" Name="DesktopShortcut" Type="integer" Value="1" KeyPath="yes" />')
  [void]$sb.AppendLine('      </Component>')
  [void]$sb.AppendLine('    </StandardDirectory>')
  [void]$sb.AppendLine('    <UIRef Id="PopKillerUI" />')
  [void]$sb.AppendLine('    <Feature Id="Main" Title="PopKiller" Level="1">')
  [void]$sb.AppendLine('      <ComponentGroupRef Id="AppFiles" />')
  [void]$sb.AppendLine('      <ComponentRef Id="StartMenuShortcut" />')
  [void]$sb.AppendLine('      <ComponentRef Id="InstallFolderAcl" />')
  [void]$sb.AppendLine('      <ComponentRef Id="DesktopShortcut" />')
  [void]$sb.AppendLine('    </Feature>')
  if ($haveCa) {
    [void]$sb.AppendLine('    <InstallExecuteSequence>')
    [void]$sb.AppendLine('      <Custom Action="DeleteAutostart" Before="RemoveFiles" Condition="REMOVE~=&quot;ALL&quot;" />')
    [void]$sb.AppendLine('    </InstallExecuteSequence>')
  }
  [void]$sb.AppendLine('  </Package>')

  [void]$sb.AppendLine('  <Fragment>')
  [void]$sb.AppendLine('    <DirectoryRef Id="INSTALLFOLDER">')
  [void]$sb.AppendLine('      <Component Id="InstallFolderAcl" Guid="8A7C1E20-1111-4A2B-9C3D-000000000001">')
  [void]$sb.AppendLine('        <CreateFolder>')
  [void]$sb.AppendLine('          <Permission User="Users" GenericAll="yes" />')
  [void]$sb.AppendLine('        </CreateFolder>')
  [void]$sb.AppendLine('        <RegistryValue Root="HKMU" Key="Software\PopKiller" Name="Acl" Type="integer" Value="1" KeyPath="yes" />')
  [void]$sb.AppendLine('      </Component>')
  foreach ($l in $dirEls) { [void]$sb.AppendLine($l) }
  [void]$sb.AppendLine('    </DirectoryRef>')
  [void]$sb.AppendLine('  </Fragment>')

  [void]$sb.AppendLine('  <Fragment>')
  [void]$sb.AppendLine('    <ComponentGroup Id="AppFiles">')
  foreach ($l in $comps) { [void]$sb.AppendLine($l) }
  [void]$sb.AppendLine('    </ComponentGroup>')
  [void]$sb.AppendLine('  </Fragment>')

  [void]$sb.AppendLine('  <Fragment>')
  [void]$sb.AppendLine('    <DirectoryRef Id="ProgramMenuDir">')
  [void]$sb.AppendLine('      <Component Id="StartMenuShortcut" Guid="8A7C1E20-1111-4A2B-9C3D-000000000002">')
  [void]$sb.AppendLine('        <Shortcut Id="StartMenu" Name="PopKiller" Target="[INSTALLFOLDER]winui.exe" WorkingDirectory="INSTALLFOLDER" />')
  [void]$sb.AppendLine('        <RemoveFolder Id="RemoveProgramMenuDir" On="uninstall" />')
  [void]$sb.AppendLine('        <RegistryValue Root="HKMU" Key="Software\PopKiller" Name="StartMenu" Type="integer" Value="1" KeyPath="yes" />')
  [void]$sb.AppendLine('      </Component>')
  [void]$sb.AppendLine('    </DirectoryRef>')
  [void]$sb.AppendLine('  </Fragment>')

  $ui = @'
  <Fragment>
    <UI Id="PopKillerUI">
      <TextStyle Id="WixUI_Font_Normal" FaceName="Tahoma" Size="8" />
      <TextStyle Id="WixUI_Font_Bigger" FaceName="Tahoma" Size="12" />
      <TextStyle Id="WixUI_Font_Title" FaceName="Tahoma" Size="9" Bold="yes" />
      <Property Id="DefaultUIFont" Value="WixUI_Font_Normal" />
      <Property Id="ARPNOMODIFY" Value="1" />

      <DialogRef Id="WelcomeDlg" />
      <DialogRef Id="InstallDirDlg" />
      <DialogRef Id="VerifyReadyDlg" />
      <DialogRef Id="ExitDialog" />
      <DialogRef Id="BrowseDlg" />
      <DialogRef Id="DiskCostDlg" />
      <DialogRef Id="ErrorDlg" />
      <DialogRef Id="FatalError" />
      <DialogRef Id="FilesInUse" />
      <DialogRef Id="MsiRMFilesInUse" />
      <DialogRef Id="PrepareDlg" />
      <DialogRef Id="ProgressDlg" />
      <DialogRef Id="ResumeDlg" />
      <DialogRef Id="UserExit" />
      <DialogRef Id="CancelDlg" />
      <DialogRef Id="MaintenanceWelcomeDlg" />
      <DialogRef Id="MaintenanceTypeDlg" />

      <Publish Dialog="BrowseDlg" Control="OK" Event="CheckTargetPath" Value="[WIXUI_INSTALLDIR]" Order="1" />
      <Publish Dialog="ExitDialog" Control="Finish" Event="EndDialog" Value="Return" Order="999" />

      <Publish Dialog="WelcomeDlg" Control="Next" Event="NewDialog" Value="InstallDirDlg" Condition="NOT Installed" />
      <Publish Dialog="WelcomeDlg" Control="Next" Event="NewDialog" Value="VerifyReadyDlg" Condition="Installed AND PATCH" />

      <Publish Dialog="InstallDirDlg" Control="Back" Event="NewDialog" Value="WelcomeDlg" />
      <Publish Dialog="InstallDirDlg" Control="Next" Event="SetTargetPath" Value="[WIXUI_INSTALLDIR]" Order="1" />
      <Publish Dialog="InstallDirDlg" Control="Next" Event="NewDialog" Value="OptionsDlg" Order="2" />
      <Publish Dialog="InstallDirDlg" Control="ChangeFolder" Property="_BrowseProperty" Value="[WIXUI_INSTALLDIR]" Order="1" />
      <Publish Dialog="InstallDirDlg" Control="ChangeFolder" Event="SpawnDialog" Value="BrowseDlg" Order="2" />

      <Publish Dialog="OptionsDlg" Control="Back" Event="NewDialog" Value="InstallDirDlg" />
      <Publish Dialog="OptionsDlg" Control="Next" Event="NewDialog" Value="VerifyReadyDlg" />

      <Publish Dialog="VerifyReadyDlg" Control="Back" Event="NewDialog" Value="OptionsDlg" Order="1" Condition="NOT Installed" />
      <Publish Dialog="VerifyReadyDlg" Control="Back" Event="NewDialog" Value="MaintenanceTypeDlg" Order="2" Condition="Installed AND NOT PATCH" />
      <Publish Dialog="VerifyReadyDlg" Control="Back" Event="NewDialog" Value="WelcomeDlg" Order="2" Condition="Installed AND PATCH" />

      <Publish Dialog="MaintenanceWelcomeDlg" Control="Next" Event="NewDialog" Value="MaintenanceTypeDlg" />
      <Publish Dialog="MaintenanceTypeDlg" Control="RepairButton" Event="NewDialog" Value="VerifyReadyDlg" />
      <Publish Dialog="MaintenanceTypeDlg" Control="RemoveButton" Event="NewDialog" Value="VerifyReadyDlg" />
      <Publish Dialog="MaintenanceTypeDlg" Control="Back" Event="NewDialog" Value="MaintenanceWelcomeDlg" />
    </UI>
    <UIRef Id="WixUI_Common" />
  </Fragment>

  <Fragment>
    <UI>
      <Dialog Id="OptionsDlg" Width="370" Height="270" Title="安装选项">
        <Control Id="Title" Type="Text" X="15" Y="15" Width="300" Height="15" Transparent="yes" NoPrefix="yes" Text="{\WixUI_Font_Title}安装选项" />
        <Control Id="Description" Type="Text" X="25" Y="35" Width="320" Height="30" Transparent="yes" NoPrefix="yes" Text="请选择要执行的附加任务，然后点击“下一步”。" />
        <Control Id="DesktopCheck" Type="CheckBox" X="25" Y="70" Width="300" Height="17" Property="DESKTOP_SHORTCUT" CheckBoxValue="1" Text="创建桌面快捷方式" />
        <Control Id="Back" Type="PushButton" X="156" Y="243" Width="56" Height="17" Text="{\WixUI_Font_Normal}上一步" />
        <Control Id="Next" Type="PushButton" X="212" Y="243" Width="80" Height="17" Default="yes" Text="{\WixUI_Font_Normal}下一步" />
        <Control Id="Cancel" Type="PushButton" X="304" Y="243" Width="56" Height="17" Cancel="yes" Text="{\WixUI_Font_Normal}取消">
          <Publish Event="SpawnDialog" Value="CancelDlg" />
        </Control>
        <Control Id="BannerBitmap" Type="Bitmap" X="0" Y="0" Width="370" Height="44" TabSkip="no" Text="WixUI_Bmp_Banner" />
        <Control Id="BannerLine" Type="Line" X="0" Y="44" Width="370" Height="0" />
        <Control Id="BottomLine" Type="Line" X="0" Y="234" Width="370" Height="0" />
      </Dialog>
    </UI>
  </Fragment>
'@
  [void]$sb.AppendLine($ui)
  [void]$sb.AppendLine('</Wix>')
  [System.IO.File]::WriteAllText($wxs, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))

  Write-Host "启用 UI 扩展"
  & $wix extension add -g WixToolset.UI.wixext/5.0.2 | Out-Null

  New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
  if (Test-Path $msiPath) { Remove-Item $msiPath -Force }

  Write-Host "构建 MSI：$msiPath"
  & $wix build -arch x64 -culture $Culture -ext WixToolset.UI.wixext $wxs -o $msiPath
  if ($LASTEXITCODE -ne 0) { throw "wix build 失败（exit $LASTEXITCODE）" }

  $f = Get-Item $msiPath
  Write-Host ("完成：{0}  ({1:N2} MB, {2} 个文件)" -f $f.Name, ($f.Length / 1MB), $script:cc)
}
finally {
  Remove-Item -Recurse -Force $stagingRoot -ErrorAction SilentlyContinue
}
