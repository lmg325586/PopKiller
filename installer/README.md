# installer/

生成 PopKiller 的 **MSI 安装包**（Windows Installer）。

## 前置
1. 已构建 Release|x64（存在 `x64\Release\winui\winui.exe`）。
2. 安装 WiX v5（dotnet 全局工具）：
   ```
   dotnet tool install --global wix --version 5.0.2
   ```
   > v6/v7 需要接受 OSMF EULA，建议用 v5。

## 使用
```
# 默认：从 x64\Release\winui 生成 dist\PopKiller-<Version>-x64.msi（中文界面）
pwsh -File installer\New-Msi.ps1

# 指定版本与输出名
pwsh -File installer\New-Msi.ps1 -Version 0.9.1 -MsiName PopKiller-Beta0.9.1-x64.msi
```

参数：`-AppOutput` `-OutputDir` `-Version` `-MsiName` `-Culture`（默认 `zh-CN`）`-UpgradeCode`。

## 生成的安装包特性
- **机器级安装**（`perMachine`，需管理员提权）；默认安装目录 `C:\PopKiller`（可在向导里修改）。
- 安装目录授予 `Users` 完全控制（核心 `Permission`），程序可在自身目录读写 `rules.json` / `winui.ini` / `blocklog.txt` / `StaticML` 等。
- 中文界面（`Language=2052` + `-culture zh-CN`）：欢迎 / 选择安装目录 / **安装选项（勾选“创建桌面快捷方式”）** / 就绪 / 进度 / 完成。
- 开始菜单快捷方式固定创建；桌面快捷方式可选。
- 随包携带 **VC++ 运行时**（app-local）；排除 `pdb/lib/exp` 与运行期文件。
- 固定 `UpgradeCode` + `MajorUpgrade`：升级时用同 `UpgradeCode` 并提升 `-Version` 即可。
- **开机自启**：安装选项“开机自动启动”（默认勾选）会在“启动”文件夹创建 `PopKiller.lnk`（MSI 跟踪）→ 卸载自动删除；程序设置页的开/关也操作同一个快捷方式。
- 安装时会清理旧版本遗留的自启项（`HKCU`/`HKLM` 的 `…\Run\PopKiller`）。
  > 注意：程序运行时产生的数据文件（`rules.json`、`winui.ini`、`blocklog.txt`、`StaticML\` 等）不在 MSI 跟踪范围内，卸载后会保留，需手动删除。

## 注意
- 机器级安装需要管理员：双击或右键“以管理员身份运行”，在 UAC 中确认。
- 脚本在临时目录搭建舞台，不修改 `x64\Release\winui` 构建输出。
