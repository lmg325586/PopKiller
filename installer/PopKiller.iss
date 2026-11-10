; PopKiller.iss —— Inno Setup 脚本（机器级安装 / 简体中文界面）
;
; 通常不直接编译本文件，而是用打包脚本生成（脚本会先用临时舞台整理文件）：
;   pwsh -File installer\New-Setup.ps1 -Version 0.11
;
; 也可单独编译（StageDir 默认指向构建输出）：
;   ISCC.exe /DAppVer=0.11 installer\PopKiller.iss
;
; 行为对齐原 MSI：
;   * 机器级安装（需管理员），默认目录 C:\PopKiller，简体中文界面
;   * 桌面快捷方式 / 开机自启 作为可选任务
;   * 安装目录授予 Users 修改权限（程序需要写 rules.json / 日志）
;   * 覆盖升级不清理运行期数据（rules.json / winui.ini / StaticML 原样保留）
;   * 卸载清理自启项、遗留启动文件夹快捷方式与运行期文件
;   * 拦截降级安装；检测到旧 MSI 安装时提示先卸载

#ifndef StageDir
  #define StageDir "..\x64\Release\winui"
#endif
#ifndef AppVer
  #define AppVer "0.11"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif
#ifndef SetupName
  #define SetupName "PopKiller-Setup"
#endif

#define AppName "PopKiller"
#define AppPublisher "lmg325586"
#define AppExe "winui.exe"
#define RunKey "Software\Microsoft\Windows\CurrentVersion\Run"
#define UninstKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\PopKiller_is1"

[Setup]
AppId=PopKiller
AppName={#AppName}
AppVersion={#AppVer}
AppVerName={#AppName} Beta {#AppVer}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/lmg325586/PopKiller
AppSupportURL=https://github.com/lmg325586/PopKiller
AppUpdatesURL=https://github.com/lmg325586/PopKiller/releases
VersionInfoVersion={#AppVer}
VersionInfoProductName={#AppName}
VersionInfoCompany={#AppPublisher}
DefaultDirName=C:\PopKiller
SetupIconFile={#SourcePath}\..\winui\Assets\app.ico
DisableProgramGroupPage=yes
DefaultGroupName={#AppName}
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutDir}
OutputBaseFilename={#SetupName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no
SetupLogging=yes
CloseApplications=yes
CloseApplicationsFilter={#AppExe}
RestartApplications=no
LicenseFile={#SourcePath}\..\LICENSE.txt
AllowNoIcons=no
DirExistsWarning=auto
UsePreviousAppDir=yes
UsePreviousTasks=yes
; 自启项按设计写 HKCU（程序自身也读写 HKCU），抑制“admin 模式使用用户区域”的构建警告
UsedUserAreasWarning=no

[Languages]
Name: "zh"; MessagesFile: "{#SourcePath}\lang\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"
Name: "autostart"; Description: "开机自动启动 PopKiller（后台静默运行）"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Dirs]
; 程序在自身目录写 rules.json / 日志 / 模型缓存，需要普通用户可写
Name: "{app}"; Permissions: users-modify

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; 开机自启（可选任务）；卸载时删除该值
Root: HKCU; Subkey: "{#RunKey}"; ValueType: string; ValueName: "{#AppName}"; ValueData: """{app}\{#AppExe}"""; Flags: uninsdeletevalue; Tasks: autostart
; 安装标记；卸载时删除整个键（顺带清理旧 MSI 留下的值）
Root: HKLM; Subkey: "Software\{#AppName}"; ValueType: string; ValueName: "Installer"; ValueData: "Inno"; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "启动 {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 卸载时清理运行期文件（升级安装不会触发卸载，数据不受影响）
Type: files; Name: "{app}\rules.json"
Type: files; Name: "{app}\rules.json.bak"
Type: files; Name: "{app}\winui.ini"
Type: files; Name: "{app}\blocklog.txt"
Type: files; Name: "{app}\labels.json"
Type: files; Name: "{app}\crash.log"
Type: files; Name: "{app}\autostart_debug.log"
Type: files; Name: "{app}\notifydiag.txt"
Type: files; Name: "{app}\*.log"
Type: files; Name: "{app}\*.dmp"
Type: filesandordirs; Name: "{app}\StaticML"

[Code]
const
  CUninstKey = '{#UninstKey}';

function VerPart(const s: String; idx: Integer): Integer;
var
  i, part, v: Integer;
begin
  v := 0;
  part := 0;
  for i := 1 to Length(s) do
  begin
    if s[i] = '.' then
    begin
      Inc(part);
      if part > idx then Break;
    end
    else if (s[i] >= '0') and (s[i] <= '9') then
    begin
      if part = idx then v := v * 10 + (Ord(s[i]) - Ord('0'));
    end;
  end;
  Result := v;
end;

function CompareVer(const a, b: String): Integer;
var
  i, x, y: Integer;
begin
  for i := 0 to 3 do
  begin
    x := VerPart(a, i);
    y := VerPart(b, i);
    if x > y then begin Result := 1; Exit; end;
    if x < y then begin Result := -1; Exit; end;
  end;
  Result := 0;
end;

function InitializeSetup(): Boolean;
var
  cur, dummy: String;
begin
  Result := True;

  { 旧版本由 MSI 安装时（会写 HKMU\Software\PopKiller 标记）→ 必须先卸载 }
  if RegQueryStringValue(HKLM, 'Software\{#AppName}', 'Acl', dummy) or
     RegQueryStringValue(HKLM, 'Software\{#AppName}', 'StartMenu', dummy) then
  begin
    MsgBox('检测到通过 MSI 安装的 PopKiller。' + #13#10#13#10 +
      '请先在“设置 → 应用”中卸载旧版，再运行本安装包。' + #13#10 +
      '注意：MSI 卸载会清理规则/配置/模型，卸载前请备份 rules.json、winui.ini 与 StaticML 文件夹。',
      mbError, MB_OK);
    Result := False;
    Exit;
  end;

  { 降级保护 }
  if RegQueryStringValue(HKLM, CUninstKey, 'DisplayVersion', cur) then
  begin
    if CompareVer('{#AppVer}', cur) < 0 then
    begin
      MsgBox('已安装更高版本的 PopKiller（' + cur + '）。' + #13#10 +
        '如确需降级，请先卸载当前版本。', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure DeleteLegacyStartupShortcuts();
begin
  DeleteFile(ExpandConstant('{userappdata}\Microsoft\Windows\Start Menu\Programs\Startup\PopKiller.lnk'));
  DeleteFile(ExpandConstant('{commonappdata}\Microsoft\Windows\Start Menu\Programs\Startup\PopKiller.lnk'));
end;

procedure DeleteMenuShortcuts();
begin
  { Inno 建的是 PopKiller.lnk；旧 MSI 建的是「PopKiller」文件夹，两种都清 }
  DeleteFile(ExpandConstant('{commonprograms}\PopKiller.lnk'));
  DeleteFile(ExpandConstant('{userprograms}\PopKiller.lnk'));
  DelTree(ExpandConstant('{commonprograms}\PopKiller'), True, True, True);
  DelTree(ExpandConstant('{userprograms}\PopKiller'), True, True, True);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    { 自启项可能由程序自己写入（未勾选安装任务），这里无条件清理 }
    RegDeleteValue(HKCU, '{#RunKey}', '{#AppName}');
    RegDeleteValue(HKLM, '{#RunKey}', '{#AppName}');
    DeleteLegacyStartupShortcuts();
    DeleteMenuShortcuts();
  end;
end;
