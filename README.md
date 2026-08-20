# PopKiller

> 基于 WinUI 3 (C++/WinRT) 的 Windows 弹窗拦截工具。
> 通过黑白名单规则、社区共享规则库与启发式特征打分，自动识别并关闭广告及流氓软件弹窗。

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.txt)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey.svg)]()
[![Language](https://img.shields.io/badge/language-C%2B%2B17-yellow.svg)]()
[![Release](https://img.shields.io/github/v/release/lmg325586/PopKiller?include_prereleases)]()
[![Community Rules](https://img.shields.io/badge/community--rules-20%2B-brightgreen)]()

---

## 目录

- [功能特性](#功能特性)
- [快速开始](#快速开始)
- [使用说明](#使用说明)
- [规则格式](#规则格式)
- [社区规则库](#社区规则库)
- [配置文件](#配置文件)
- [日志格式](#日志格式)
- [编译指南](#编译指南)
- [项目结构](#项目结构)
- [路线图](#路线图)
- [贡献规则](#贡献规则)
- [免责声明](#免责声明)
- [许可证](#许可证)

---

## 功能特性

| 模块 | 说明 |
| :--- | :--- |
| **规则引擎** | 支持 **进程名 / 完整路径 / 窗口标题 / 窗口类名** 四种匹配字段 |
| **匹配模式** | 包含（子串）/ 精确（全等）/ 通配符（`*` 与 `?`） |
| **黑白名单** | 黑名单拦截，白名单放行，*白名单优先级最高* |
| **社区规则库** | 启动时联网拉取仓库 `community_rules.json`，与本地规则合并生效，离线使用缓存 |
| **启发式打分** | 对 20 余项特征（窗口样式、进程路径、数字签名、进程年龄等）加权评分 |
| **窗口拾取** | 点选目标窗口，提取进程 / 路径 / 类名 / 标题，一键生成规则 |
| **系统托盘** | 最小化到托盘，右键菜单快速切换拦截状态 |
| **日志系统** | 实时刷新拦截日志，支持详细日志模式记录每个窗口的打分明细 |
| **主题切换** | 支持普通 / Mica 材质两种界面风格 |

---

## 快速开始

### 下载使用

从 [Releases](https://github.com/lmg325586/PopKiller/releases) 页面下载最新版压缩包，解压后运行 `winui.exe` 即可。

> 当前为 Beta 阶段，仅在 Windows 11 x64 下充分测试；Windows 10 可运行但 UI 可能存在显示问题。

### 首次使用

1. 运行程序，在「弹窗拦截」页打开总开关；
2. 程序会自动从 GitHub 拉取最新社区规则库（首次启动需联网）；
3. 点击「选取」，点选目标弹窗，自动填充窗口信息；
4. 选择 **名单类型 / 匹配字段 / 匹配模式**，输入模式后点击「添加」；
5. 在「设置」页调整启发式模式与详细日志开关。

---

## 使用说明

### 添加规则

1. 进入「弹窗拦截」页面；
2. 点击「选取」按钮，鼠标变为十字准星，点选目标窗口；
3. 拾取后自动填充进程名、路径、类名、标题信息；
4. 在下拉框中选择：
   - **名单类型**：黑名单（拦截）/ 白名单（放行）
   - **匹配字段**：进程 / 路径 / 标题 / 类名
   - **匹配模式**：包含 / 精确 / 通配符
5. 确认模式内容后点击「添加」。

> 提示：如果添加的规则与已有相反名单规则内容相同，会弹出橙色警告——白名单优先级更高，该窗口将被放行。

### 搜索与管理

- 在规则列表上方的搜索框输入关键词，可实时过滤规则；
- 选中规则后点击「删除」移除该规则；
- 所有本地规则自动保存到程序同目录的 `rules.json`。

### 社区规则库

- 程序启动时自动从 GitHub 拉取最新 `community_rules.json`；
- 社区规则与本地规则合并生效，白名单优先级始终最高；
- 社区规则为只读，无法在界面中修改或删除；
- 如需排除某条社区规则，可添加一条相反的白名单规则覆盖；
- 离线启动时使用上次缓存的社区规则。

### 启发式模式

在「设置」页面可切换启发式引擎工作模式：

| 模式 | 说明 |
| :--- | :--- |
| **关闭** | 仅按黑白名单规则拦截，不进行启发式评估 |
| **仅记录** | 对所有窗口打分并写入日志，但不自动拦截（用于调参和观察） |
| **自动拦截** | 分数达到阈值（默认 70）的窗口自动关闭 |

### 系统托盘

- 点击窗口最小化按钮，程序自动隐藏到系统托盘；
- 左键单击托盘图标恢复主窗口；
- 右键单击托盘图标弹出菜单，可快速切换拦截状态、显示主窗口或退出程序。

---

## 规则格式

规则以 JSON 格式保存在程序同目录的 `rules.json` 中：

```json
{
    "version": 1,
    "rules": [
        {
            "list": "B",
            "field": "exe",
            "mode": "contains",
            "pattern": "flashcenter.exe"
        },
        {
            "list": "W",
            "field": "exe",
            "mode": "contains",
            "pattern": "explorer.exe"
        }
    ]
}
```

| 字段 | 取值 | 说明 |
| :--- | :--- | :--- |
| `list` | `B` / `W` | 黑名单 / 白名单 |
| `field` | `exe` / `path` / `title` / `class` | 进程名 / 完整路径 / 窗口标题 / 窗口类名 |
| `mode` | `contains` / `exact` / `wildcard` | 包含 / 精确匹配 / 通配符 |
| `pattern` | 字符串 | 匹配内容（不区分大小写） |

**通配符说明：**
- `*` 匹配任意长度的任意字符（包括空字符）
- `?` 匹配单个任意字符
- 匹配不区分大小写

---

## 社区规则库

PopKiller 内置社区规则共享机制，让所有用户受益于集体维护的规则库。

### 工作原理

1. 程序启动时，从 GitHub 仓库根目录拉取 `community_rules.json`；
2. 拉取成功后缓存到本地，后续离线启动时使用缓存版本；
3. 社区规则与用户本地规则**合并生效**，白名单优先级始终最高；
4. 社区规则为只读，用户无法在界面中修改或删除；如需排除某条社区规则，可添加一条相反的白名单规则覆盖。

### community_rules.json 格式

与本地 `rules.json` 格式一致：

```json
{
    "version": 1,
    "rules": [
        { "list": "B", "field": "exe",   "mode": "contains", "pattern": "flashcenter.exe" },
        { "list": "B", "field": "title", "mode": "contains", "pattern": "热点" },
        { "list": "W", "field": "exe",   "mode": "contains", "pattern": "wechat.exe" }
    ]
}
```

### 贡献社区规则

欢迎提交 PR 维护 `community_rules.json`：

1. Fork 本仓库；
2. 编辑根目录下的 `community_rules.json`，按格式添加规则；
3. 提交 PR，描述规则对应的弹窗来源和验证情况；
4. 审核通过后合并，所有用户下次启动即可自动获取新规则。

> 请勿提交针对正常软件的误杀规则。系统进程（explorer.exe、dwm.exe 等）已内置白名单保护，无需重复添加。

---

## 配置文件

程序同目录下的 `winui.ini` 保存所有配置：

| Section | Key | 默认值 | 说明 |
| :--- | :--- | :--- | :--- |
| Blocker | Enabled | 0 | 拦截总开关（0=关，1=开） |
| Blocker | ForceBlock | 0 | 强制拦截（忽略弹窗形态判断，命中黑名单即关） |
| Blocker | HeuristicMode | 0 | 启发式模式（0=关闭，1=仅记录，2=自动拦截） |
| Blocker | HeuristicThreshold | 70 | 启发式拦截分数阈值 |
| Blocker | VerboseLog | 0 | 详细日志（0=仅记录拦截，1=记录所有窗口） |
| UI | Material | 0 | 界面材质（0=普通，1=Mica） |

本地规则保存在同目录的 `rules.json`，拦截日志保存在 `blocklog.txt`。

---

## 日志格式

拦截日志保存在程序同目录的 `blocklog.txt`，UTF-8 编码（带 BOM），实时追加。

**示例：**

```text
2026-08-20 14:30:15 action=block | reason=heuristic(75) | owner+15 toolwin+12 topmost+20 notresizable+12 nominmax+12 small+28 notitle+20 young+5 unsigned+12 | title= | class=WindowsForms10.Window.8.app.0.1234567 | exe=adpopup.exe
2026-08-20 14:30:20 action=allow | reason=whitelist | title=文件资源管理器 | class=CabinetWClass | exe=explorer.exe
```

| 字段 | 说明 |
| :--- | :--- |
| 时间戳 | `YYYY-MM-DD HH:MM:SS` 格式的本地时间 |
| `action` | `block`（拦截）/ `monitor`（观察）/ `allow`（白名单放行） |
| `reason` | `blacklist` / `whitelist` / `community` / `heuristic(分数)` |
| 明细 | 启发式各项得分，如 `toolwin+12`、`signed-5`（仅启发式命中时出现） |
| `title` | 窗口标题（小写） |
| `class` | 窗口类名（小写） |
| `exe` | 进程名（小写） |

在「拦截日志」页面可实时查看日志，支持手动刷新和清空。

---

## 编译指南

### 编译环境

| 项目 | 要求 |
| :--- | :--- |
| IDE | Visual Studio 2026 |
| 工作负载 | 使用 C++ 的桌面开发，C++ winui开发 |
| SDK | Windows App SDK (WinUI 3) 1.4+ |
| 语言标准 | C++17 及以上 |
| 系统 | Windows 10 1809+ / Windows 11 |

### 第三方依赖

- [nlohmann/json](https://github.com/nlohmann/json) — JSON 解析（已包含在 `vendor/` 目录）
- Windows App SDK — WinUI 3 框架（NuGet 自动还原）

### 编译步骤

```bash
# 克隆仓库
git clone https://github.com/lmg325586/PopKiller.git
cd PopKiller
```

1. 双击 `winui.slnx` 打开解决方案；
2. 选择 `x64 / Release` 配置；
3. 生成解决方案（首次生成会自动还原 NuGet 包，包括 Windows App SDK）；
4. 输出目录下的 `winui.exe` 即为可执行文件。

> 注意：运行时需要 [Windows App SDK 运行时](https://learn.microsoft.com/zh-cn/windows/apps/windows-app-sdk/downloads)。Release 打包中已包含完整运行时，自行编译时如提示缺少运行时，请安装对应版本。

---

## 项目结构

```
PopKiller/
├── community_rules.json      # 社区共享规则库
├── README.md                  # 项目说明
├── LICENSE.txt                # MIT 许可证
├── winui.slnx                 # Visual Studio 解决方案
└── winui/                     # 主项目
    ├── App.xaml(.cpp/.h)      # 应用入口
    ├── MainWindow.xaml(.cpp/.h) # 主窗口（导航、托盘、最小尺寸）
    ├── HomePage.xaml(.cpp/.h)   # 首页（系统信息、辉光效果）
    ├── PopupBlockerPage.xaml(.cpp/.h) # 拦截规则页
    ├── SettingsPage.xaml(.cpp/.h)     # 设置页
    ├── BlockLogPage.xaml(.cpp/.h)     # 拦截日志页
    ├── LicensePage.xaml(.cpp/.h)      # 许可证页
    ├── PopupBlocker.h          # 拦截引擎核心（钩子、匹配、日志、强杀）
    ├── HeuristicScorer.h       # 启发式打分引擎
    ├── RuleTypes.h             # 规则类型定义
    ├── RuleStorage.h           # 规则 JSON 读写存储
    ├── WindowPicker.h          # 窗口拾取器
    ├── TrayIcon.h              # 系统托盘
    ├── AppSettings.h           # 配置读写（ini）
    ├── AppTheme.h              # 主题与标题栏
    ├── vendor/json.hpp         # nlohmann/json 库
    └── Assets/                  # 应用图标资源
```

---

## 路线图

- [x] 黑白名单规则引擎
- [x] 通配符与路径匹配
- [x] 窗口拾取工具
- [x] 启发式特征打分
- [x] 数字签名校验
- [x] 详细日志开关
- [x] 系统托盘支持
- [x] 规则存储 JSON 化
- [x] 社区规则库联网拉取与合并
- [ ] 拦截阈值滑条 UI
- [ ] 规则导入/导出

---

## 贡献规则

欢迎贡献代码、规则和反馈：

1. **社区规则**：提交 PR 修改 `community_rules.json`，请在描述中说明规则对应的弹窗来源；

---

## 免责声明

> 本工具仅供学习与研究使用。
>
> 启发式打分基于经验权重，存在误判可能；社区规则由用户贡献，未经逐一验证。
>
> 使用本工具造成的任何直接或间接损失，作者不承担责任。
>
> 如遇正常软件被误拦截，请添加白名单规则，或提交 Issue / PR 帮助改进。

---

## 许可证

[MIT License](LICENSE.txt)