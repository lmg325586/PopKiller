# PopKiller

> 基于 WinUI 3 (C++/WinRT) 的 Windows 弹窗拦截工具。
> 通过黑白名单规则与启发式特征打分，自动识别并关闭广告及流氓软件弹窗。

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.txt)
[![Platform](https://img.shields.io/badge/platform-Windows%2011-lightgrey.svg)]()
[![Language](https://img.shields.io/badge/language-C%2B%2B17-yellow.svg)]()

---

## 目录

- [功能](#功能)
- [编译环境](#编译环境)
- [依赖的 NuGet 包](#依赖的-nuget-包)
- [生成项目](#生成项目)
- [使用说明](#使用说明)
- [规则格式](#规则格式)
- [配置文件](#配置文件)
- [日志格式](#日志格式)
- [路线图](#路线图)
- [免责声明](#免责声明)
- [许可证](#许可证)

---

## 功能

| 模块 | 说明 |
| :--- | :--- |
| 规则引擎 | 支持 **进程名 / 完整路径 / 窗口标题 / 窗口类名** 四种匹配字段 |
| 匹配模式 | 包含（子串）/ 精确（全等）/ 通配符（`*` 与 `?`） |
| 黑白名单 | 黑名单拦截，白名单放行，*白名单优先级最高* |
| 启发式打分 | 对 20 余项特征（窗口样式、进程路径、数字签名等）加权评分 |
| 窗口拾取 | 点选目标窗口，提取进程 / 路径 / 类名，辅助生成规则 |
| 顽固强杀 | 对无视关闭消息的黑名单进程延迟强杀（带系统目录安全锁） |
| 日志系统 | 支持详细日志，记录每个窗口的打分明细 |

## 编译环境

| 项目 | 要求 |
| :--- | :--- |
| IDE | Visual Studio 2026 |
| 工作负载 | 使用 C++ 的桌面开发 |
| SDK | Windows App SDK (WinUI 3) |
| 语言标准 | C++17 及以上 |
| 系统 | Windows 11 |

## 依赖的 NuGet 包

项目通过 NuGet 引用以下包（具体版本以 `winui/winui.vcxproj` 中的 `PackageReference` 为准）：

| 包名 | 版本 | 用途 |
| :--- | :--- | :--- |
| Microsoft.Windows.CppWinRT | 3.0.260715.1 | C++/WinRT 语言投影与 `.g.h/.g.cpp` 代码生成 |
| Microsoft.WindowsAppSDK | *见 vcxproj* | WinUI 3 运行时、窗口与控件 API |
| Microsoft.Windows.SDK.BuildTools | 传递依赖 | WinRT 元数据与构建工具 |
| Microsoft.Web.WebView2 | 传递依赖 | Windows App SDK 传递依赖 |

> `packages/` 目录不入库，克隆后需联网还原。

## 生成项目

```bash
# 克隆仓库
git clone https://github.com/lmg325586/PopKiller.git
cd PopKiller
```

1. 双击 `winui.slnx` 打开解决方案；
2. 右键解决方案 → **还原 NuGet 包**（首次生成时 VS 通常也会自动还原）；
3. 选择 `x64 / Release`（调试可选 `Debug`）；
4. 点击 **生成解决方案**。

> **提示**：若遇到 `C1060 编译器的堆空间不足`，请在 `winui/winui.vcxproj` 的任意 `<PropertyGroup>` 中添加：
> ```xml
> <PreferredToolArchitecture>x64</PreferredToolArchitecture>
> ```
> 切换到 64 位编译器工具集后重新生成即可解决。

## 使用说明

1. 运行程序，在“弹窗拦截”页打开总开关；
2. 点击“选取”，点选目标弹窗，自动填充窗口信息；
3. 选择 **名单类型 / 匹配字段 / 匹配模式**，输入模式后点击“添加”；
4. 在“设置”页调整启发式模式（关闭 / 启用 / 仅记录）与详细日志开关。

## 规则格式

规则以四段式保存在 `winui.ini` 中：

```text
[名单类型]:[匹配字段]:[匹配模式]:[模式内容]
```

| 段 | 取值 | 说明 |
| :--- | :--- | :--- |
| 名单类型 | `B` / `W` | 黑名单 / 白名单 |
| 匹配字段 | `exe` / `path` / `title` / `class` | 进程名 / 路径 / 标题 / 类名 |
| 匹配模式 | `contains` / `exact` / `wildcard` | 包含 / 精确 / 通配符 |

示例：

```ini
Rule0=B:exe:contains:flashcenter.exe
Rule1=W:path:exact:c:\windows\system32\cmd.exe
Rule2=B:title:wildcard:*热点*
```

## 配置文件

`winui.ini`（与程序同目录）：

| Section | Key | 默认值 | 说明 |
| :--- | :--- | :--- | :--- |
| Blocker | Enabled | 0 | 总开关 |
| Blocker | ForceBlock | 0 | 强制拦截（忽略弹窗形态判断） |
| Blocker | HeuristicMode | 0 | 0=关闭，1=仅记录，2=自动拦截 |
| Blocker | HeuristicThreshold | 70 | 启发式拦截阈值 |
| Blocker | VerboseLog | 0 | 详细日志（记录所有窗口） |
| Blocker | RuleCount | 0 | 规则数量 |

## 日志格式

`blocklog.txt` 示例：

```text
2026-08-17 22:47:41 action=block | reason=heuristic(75) | owner+15 toolwin+25 notresizable+10 nominmax+10 notitle+10 young+10 signed-15 | title= | class=windowsforms10... | exe=pwsh.exe
```

| 字段 | 说明 |
| :--- | :--- |
| action | `block`（拦截）/ `monitor`（观察）/ `allow`（白名单放行） |
| reason | `blacklist` / `whitelist` / `heuristic(分数)` |
| 明细 | 启发式各项得分，如 `toolwin+25`、`signed-15` |

## 路线图

- [x] 黑白名单规则引擎
- [x] 通配符与路径匹配
- [x] 窗口拾取工具
- [x] 启发式特征打分
- [x] 数字签名校验
- [x] 详细日志开关
- [ ] 拦截阈值滑条 UI
- [ ] 特征 CSV 导出与标注（数据收集）

## 免责声明

> 本工具仅供学习与研究使用。启发式打分基于经验权重，存在误判可能；
> 请谨慎开启“自动拦截”与“顽固强杀”功能。因使用本工具造成的任何数据丢失或系统异常，作者不承担责任。

## 许可证

基于 [MIT License](LICENSE.txt) 开源。