# 函数输入输出参考
> 基于当前 `master` 分支（Beta 0.6）代码整理。

## AppSettings.h（配置读写）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `IniPath()` | 无 | `std::wstring` | exe 同目录 `winui.ini` 路径 |
| `ReadInt(section, key, def)` | 段、键、默认值 | `int` | 读 ini |
| `WriteInt(section, key, value)` | 段、键、值 | `void` | 写 ini |
| `ReadString(section, key, def)` | 段、键、默认串 | `std::wstring` | 堆缓冲读取，上限 4095 字符 |
| `WriteString(section, key, value)` | 段、键、值 | `void` | 写 ini |
| `DeleteKey(section, key)` | 段、键 | `void` | 删 ini 键 |

## RuleTypes.h（规则类型与工具）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `Lower(s)` | 原串 | `std::wstring` | 小写化 |
| `RuleKey(r)` | 规则 | `std::wstring` | 生成规则唯一键（list+field+mode+pattern） |

全局类型：`Rule{ list, field, mode, pattern, fromCommunity }`、`RuleList`(B/W)、`RuleField`(Exe/Path/Title/Class)、`RuleMode`(Contains/Exact/Wildcard)。

## RuleStorage.h（规则 JSON 存储）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `RulesPath()` | 无 | `std::wstring` | exe 同目录 `rules.json` 路径 |
| `WStringToUtf8(wstr)` | 宽串 | `std::string` | UTF-8 转换 |
| `Utf8ToWString(str)` | UTF-8 串 | `std::wstring` | 宽串转换 |
| `ReadFileToUtf8String(p, out)` | 路径、out | `bool` | 读文件为 UTF-8 |
| `WriteUtf8StringToFile(p, text)` | 路径、内容 | `bool` | 写 UTF-8 文件 |
| `ParseRuleLine(line, r)` | 旧格式行、out | `bool` | 兼容旧 ini 格式单行解析 |
| `ParseRulesFromJsonString(utf8_text, out)` | JSON 串、out | `bool` | 解析规则 JSON |
| `LoadRulesJson(out, removedOut)` | rules out、removed out | `bool` | 从 `rules.json` 加载 |
| `SerializeRules(rules, removed)` | 规则列表、墓碑列表 | `std::string` | 序列化为 JSON 串 |
| `SaveRulesJson(rules, removed)` | 规则列表、墓碑列表 | `bool` | 写入 `rules.json` |
| `EnsureDefaultRules()` | 无 | `void` | 首运行写默认规则（`Initialized` 标志） |

## LabelStorage.h（标注数据存储）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `LabelsPath()` | 无 | `std::wstring` | exe 同目录 `labels.json` 路径 |
| `ExtractField(s, key)` | 日志行、字段名 | `std::wstring` | 从 `key=value` 格式提取字段 |
| `ParseLine(line)` | 日志行 | `Sample` | 解析日志行为训练样本 |
| `Load(out)` | out map | `void` | 从 `labels.json` 加载标注（异常时静默） |
| `Save(m)` | 标注 map | `bool` | 保存标注到 `labels.json` |
| `ExportJson(m)` | 标注 map | `std::string` | 导出为训练样本 JSON |
| `Clear(m)` | 标注 map | `void` | 清空标注缓存 |

全局类型：`Sample{ exe, title, class, raw, score, label, action, reason }`。

## PopupBlocker.h（拦截引擎核心）

### 全局状态

| 变量 | 类型 | 说明 |
|---|---|---|
| `Rules` | `std::vector<Rule>` | 规则列表（含社区规则） |
| `RulesMutex` | `std::mutex` | 规则读写锁 |
| `CommunityRemoved` | `std::vector<std::wstring>` | 社区规则删除偏好（墓碑） |
| `Running` | `std::atomic<bool>` | 引擎运行状态 |
| `ForceBlock` | `bool` | 强制拦截（命中即关） |
| `SelfExe` | `std::wstring` | 自身 exe 小写名 |
| `HeuristicMode` | `int` | 启发式模式（0关/1仅记录/2自动拦截） |
| `HeuristicThreshold` | `int` | 启发式拦截阈值（默认 70） |
| `VerboseLog` | `bool` | 详细日志开关 |
| `MLHeuristic` | `bool` | 机器学习识别开关（仅记录） |
| `ToastNotify` | `bool` | 拦截通知开关（默认开） |
| `EnabledChangedCallback` | `std::function<void()>` | 拦截状态变更回调 |
| `CommunityRulesFetchCallback` | `std::function<void(bool, std::wstring)>` | 社区规则拉取完成回调 |
| `BlockOccurredCallback` | `std::function<void(exeName, windowTitle, matchResult)>` | 拦截发生回调（用于 Toast 通知） |

### 公共函数

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `LogPath()` | 无 | `std::wstring` | `blocklog.txt` 路径 |
| `WildcardMatch(str, pat)` | 目标串、模式 | `bool` | 大小写不敏感，支持 `*`/`?` |
| `InitSelfExe()` | 无 | `void` | 置 `SelfExe`（自身 exe 小写名） |
| `LooksLikePopup(hwnd)` | 窗口句柄 | `bool` | owner/toolwin/不可调且无最小化 |
| `SaveRules(newRules)` | 规则列表 | `void` | 保存规则并刷新引擎缓存 |
| `AddWhitelistExe(exe)` | exe 名 | `bool` | 添加白名单进程（去重） |
| `SyncFromSettings()` | 无 | `void` | 刷新所有配置缓存（含 ToastNotify） |
| `FetchCommunityRulesAsync()` | 无 | `IAsyncAction` | 联网拉取社区规则并合并 |
| `Start()` | 无 | `void` | 启动钩子线程（`Running` 置位） |
| `Stop()` | 无 | `void` | `WM_QUIT` + join 线程 |
| `WinEventProc(hook, event, hwnd, idObject, idChild, thread, time)` | 事件参数 | `void` | 入口：过滤→评估→执行→日志→通知 |

### detail 命名空间

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `GetProcessName(hwnd)` | 句柄 | `std::wstring` | 小写 exe 名 |
| `GetProcessPath(hwnd)` | 句柄 | `std::wstring` | 小写完整路径 |
| `IsProtected(hwnd)` | 句柄 | `bool` | 自身 + 系统进程 + 浏览器白名单 |
| `GetTitle(hwnd)` | 句柄 | `std::wstring` | 小写标题（截断 256） |
| `GetClass(hwnd)` | 句柄 | `std::wstring` | 小写类名 |
| `MatchRule(hwnd, r, exe&, path&, title&, cls&)` | 句柄、规则、4 缓存串(in/out) | `bool` | 单规则匹配 |
| `Match(hwnd)` | 句柄 | `int` | 0=未命中 1=白 2=黑 |
| `Log(s)` | 日志行 | `void` | 追加 UTF-8 带时间戳行（新文件写 BOM） |
| `PassEventFilter(hwnd, idObject, idChild)` | 句柄、对象、子ID | `bool` | 事件预过滤（跳过自身/非窗口/对象） |
| `EvaluateWindow(hwnd, idEventTime)` | 句柄、事件时间 | `EventVerdict` | 综合评估：规则匹配+启发式+ML+raw |
| `WriteEventLog(hwnd, idEvent, v)` | 句柄、事件、评估结果 | `void` | 写日志（含启发式明细+raw+ml） |
| `ScheduleForceKill(hwnd)` | 句柄 | `void` | 延迟 400ms 强杀进程线程（非系统路径） |
| `EnforceBlock(hwnd, matchResult)` | 句柄、匹配结果 | `void` | 关闭/隐藏窗口，黑名单起强杀，触发通知回调 |
| `ThreadMain(lp)` | 无 | `DWORD` | 挂 SHOW/FOREGROUND 双钩子 + 消息循环 |

输出结构：`EventVerdict{ action, reason, detail, shouldBlock, shouldLog, matchResult }`。

## HeuristicScorer.h（启发式打分）

### detail 命名空间

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `Lower(s)` | 原串 | `std::wstring` | 小写化（独立副本） |
| `GetProcessPath(hwnd)` | 句柄 | `std::wstring` | 小写完整路径 |
| `GetTitle(hwnd)` | 句柄 | `std::wstring` | 小写标题 |
| `GetClass(hwnd)` | 句柄 | `std::wstring` | 小写类名 |

### 公共函数

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `DigitRatio(s)` | 串 | `float` | 数字占比 |
| `HexRatio(s)` | 串 | `float` | 十六进制字符占比 |
| `ProcessAgeSeconds(hwnd)` | 句柄 | `float` | 进程年龄秒，失败 -1 |
| `IsFileSigned(path)` | 路径 | `bool` | WinVerifyTrust；有签名即 true |
| `IsFileSignedCached(path)` | 路径 | `bool` | 带 `SigCache`+互斥锁缓存 |
| `ExtractFeatures(hwnd)` | 句柄 | `Features` | 21 项特征（含 `path`、`cls`） |
| `BuildRawBits(f, rc, evTime)` | 特征、窗口矩形、事件时间 | `std::wstring` | 17 位 T/F 特征串（含空闲/鼠标距离） |
| `ScoreWindow(f, detail&)` | 特征、明细串(out) | `int` | ≥0 分数；硬过滤时返回 0 且 detail 为 skip 标记 |

全局状态：`g_w`（权重表）、`SigCache`。
输出结构：`Features{ hasOwner, toolWin, topmost, noActivate, resizable, hasMinMax, captionSysmenu, wNorm, hNorm, titleLen, titleEmpty, titleDigitRatio, titleKwHits, clsLen, clsHexRatio, pathTemp, pathRoaming, pathDepth, exeDigitRatio, procAgeSec, path, cls }`（均为 float + 2 个 wstring）。

## HeuristicML.h（静态机器学习）

### detail 命名空间

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `Lower(s)` | 原串 | `std::wstring` | 小写化（独立副本） |
| `GetTitle(hwnd)` | 句柄 | `std::wstring` | 小写标题 |
| `GetClass(hwnd)` | 句柄 | `std::wstring` | 小写类名 |
| `GetProcessName(hwnd)` | 句柄 | `std::wstring` | 小写 exe 名 |

### MLEngine 类

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `GetInstance()` | 无 | `MLEngine&` | 单例 |
| `IsEnabled()` | 无 | `bool` | 模型是否加载成功 |
| `EnsureLoaded()` | 无 | `bool` | 首次调用时加载两个 ONNX 模型 |
| `Predict(hwnd)` | 句柄 | `int` | 1=弹窗 0=非弹窗 -1=失败（双模型 AND 投票） |
| `ExtractFeatures(hwnd, features&)` | 句柄、特征数组(out) | `bool` | 提取 23 维特征到 float 数组 |

全局状态：`GOOD_EXES`（27 个正常软件白名单数组）。
模型文件：`popup_rf.onnx`（随机森林）、`popup_lr.onnx`（逻辑回归），位于 `StaticML\` 目录。

## FilePicker.h（公共文件选择器）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `GetAppHwnd()` | 无 | `HWND` | 获取主窗口句柄（通过 App::window） |
| `PickJsonFile(save)` | true=保存 false=打开 | `std::wstring` | 弹出 JSON 文件选择对话框，取消返回空串 |

## WindowPicker.h（窗口拾取）

### detail 命名空间

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `FrameProc(h, msg, wp, lp)` | 消息 | `LRESULT` | DefWindowProc |
| `EnsureFrame()` | 无 | `void` | 建穿透蓝框层 |
| `PaintFrame()` | 无 | `void` | UpdateLayeredWindow 画蓝边 |
| `MoveFrame()` | 无 | `void` | 蓝框贴合 `Target` |
| `EnsureOverlay()` | 无 | `void` | 全屏遮罩（alpha=10） |
| `Teardown(commit)` | 是否提交 | `void` | 隐藏窗口、释放捕获/热键；commit 时填 `PickResult` 回调 |
| `OverlayProc(h, msg, wp, lp)` | 消息 | `LRESULT` | 移动高亮/左键提交/右键·ESC 取消 |

### 公共函数

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `Start(mainHwnd, cb)` | 主窗口句柄、回调 | `void` | 显示遮罩、注册 ESC 热键、SetCapture |
| `Cancel()` | 无 | `void` | `Teardown(false)` |

输出结构：`PickResult{ exe, processPath, title, className }`。

## AutoStart.h（开机自启）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `IsEnabled()` | 无 | `bool` | 检查注册表 Run 键是否存在 |
| `Enable()` | 无 | `bool` | 写入注册表 Run 键（当前用户） |
| `Disable()` | 无 | `bool` | 删除注册表 Run 键 |
| `SetEnabled(enable)` | bool | `bool` | 统一开关接口 |

全局常量：`RunKeyPath`、`ApprovedPath`（StartupApproved 兼容）。

## TrayIcon.h（系统托盘）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `Create(hwnd)` | 主窗口句柄 | `bool` | 创建托盘图标（NOTIFYICONDATA） |
| `Remove()` | 无 | `void` | 删除托盘图标 |
| `ShowContextMenu(hwnd)` | 窗口句柄 | `void` | 弹出右键菜单（拦截开关/显示/退出） |
| `HandleCommand(hwnd, id)` | 窗口句柄、菜单ID | `bool` | 处理菜单命令 |

## AppTheme.h（主题与标题栏）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `ApplyTheme(hwnd, material)` | 句柄、材质(0普通/1Mica) | `void` | 应用背景材质与标题栏颜色 |
| `SetTitleBarColor(hwnd)` | 句柄 | `void` | 设置标题栏暗色（DwmSetWindowAttribute） |

## MainWindow.xaml.cpp（主窗口）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `NavView_SelectionChanged(sender, args)` | 导航视图选中 | `void` | 根据 tag 导航到对应页面 |
| `NavigateFrameToTag(tag)` | 页面 tag 字符串 | `void` | ContentFrame 导航（Home/Blocker/BlockLog/Settings） |
| `NavigateToTag(tag)` | 页面 tag 字符串 | `void` | 外部调用入口（托盘菜单等），同步选中 NavView |

## HomePage.xaml.cpp（主页）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `RootPointerMoved(sender, e)` | 指针移动 | `void` | 更新辉光效果位置（5 层椭圆渐变） |
| `RootPointerExited(sender, e)` | 指针离开 | `void` | 隐藏辉光 |
| `GoToBlocker_Tapped(sender, e)` | 卡片点击 | `void` | 导航到弹窗拦截页 |
| `GoToSettings_Tapped(sender, e)` | 卡片点击 | `void` | 导航到设置页 |

## PopupBlockerPage.xaml.cpp（规则页）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `ReloadRulesFromEngine()` | 无 | `void` | 从引擎重新加载规则到本地缓存 |
| `RefreshList()` | 无 | `void` | 按 `m_searchText` 重建列表，更新 `m_visibleIndex` 映射 |
| `Save()` | 无 | `void` | 四段式写回 ini，清理多余 `RuleN` |
| `EnableToggle_Toggled(sender, args)` | 开关 | `void` | 写 `Enabled`，Start/Stop 引擎 |
| `CommunityRulesToggle_Toggled(sender, args)` | 开关 | `void` | 写社区规则开关，触发拉取 |
| `UpdateCommunityStatus(ok, msg)` | 成功标志、消息 | `void` | 更新社区规则状态文本与重试按钮 |
| `RetryFetchButton_Click(sender, args)` | 按钮 | `void` | 重新拉取社区规则 |
| `EditRule_Click(sender, args)` | 按钮 | `void` | 选中规则回填到输入框，切换为保存模式 |
| `AddRule_Click(sender, args)` | 按钮 | `void` | 冲突检测→加规则→Save→Sync→刷新；冲突时橙色提示 |
| `DeleteRule_Click(sender, args)` | 按钮 | `void` | 经 `m_visibleIndex` 映射删除 |
| `Pick_Click(sender, args)` | 按钮 | `void` | 启动拾取器，回填输入框与 PickInfo |
| `SearchInput_TextChanged(sender, args)` | 文本 | `void` | 更新 `m_searchText`→RefreshList |
| `OpenIO_Click(sender, args)` | 按钮 | `void` | 导航到规则导入导出页 |

## RuleIOPage.xaml.cpp（导入导出页）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `BackButton_Click(sender, args)` | 按钮 | `void` | 返回上一页 |
| `ExportButton_Click(sender, args)` | 按钮 | `void` | 选择保存路径→按选项过滤→写 JSON→显示结果 |
| `ImportButton_Click(sender, args)` | 按钮 | `void` | 选择文件→解析→去重合并→保存→显示结果 |

## SettingsPage.xaml.cpp（设置页）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `LicenseLink_Click(sender, args)` | 链接 | `void` | 导航到许可证页 |
| `ThemeComboBox_SelectionChanged(sender, args)` | 下拉 | `void` | 写主题、应用背景与标题栏 |
| `ForceBlockToggle_Toggled(sender, args)` | 开关 | `void` | 写 `ForceBlock` 并同步引擎 |
| `HeuristicModeCombo_SelectionChanged(sender, args)` | 下拉 | `void` | 下标↔模式映射（0/2/1）写 ini + Sync |
| `VerboseLogToggle_Toggled(sender, args)` | 开关 | `void` | 写 `VerboseLog` + Sync |
| `AutoStartToggle_Toggled(sender, args)` | 开关 | `void` | 调用 AutoStart::SetEnabled |
| `MLHeuristicToggle_Toggled(sender, args)` | 开关 | `void` | 写 `MLHeuristic` + Sync |
| `ToastNotifyToggle_Toggled(sender, args)` | 开关 | `void` | 写 `ToastNotify` + 同步引擎变量 |

## BlockLogPage.xaml.cpp（日志页）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `ReloadFromFile()` | 无 | `void` | 从 `blocklog.txt` 读取原始行到 `m_rawLines` |
| `ApplyFilter()` | 无 | `void` | 按当前过滤标签+搜索词过滤 `m_rawLines`，更新列表 |
| `Load()` | 无 | `void` | 读文件→应用过滤→刷新列表 |
| `Filter_Changed(sender, args)` | 下拉 | `void` | 切换过滤标签→ApplyFilter |
| `Search_Changed(sender, args)` | 文本 | `void` | 更新搜索词→ApplyFilter |
| `Timer_Tick(sender, args)` | 定时器 | `void` | 自动刷新日志（默认 2 秒间隔） |
| `Refresh_Click(sender, args)` | 按钮 | `void` | 手动刷新 |
| `Clear_Click(sender, args)` | 按钮 | `void` | 清空日志文件 |
| `LogItem_RightTapped(sender, args)` | 列表项右键 | `void` | 弹出标注/加规则上下文菜单 |
| `MarkPopup_Click(sender, args)` | 菜单 | `void` | 标记选中行为弹窗（label=popup） |
| `MarkNotPopup_Click(sender, args)` | 菜单 | `void` | 标记选中行为非弹窗（label=notpopup） |
| `ExportSamples_Click(sender, args)` | 菜单 | `void` | 导出标注样本为 JSON，清空标注缓存 |
| `AddToBlacklist_Click(sender, args)` | 菜单 | `void` | 将选中行 exe 加入黑名单 |
| `AddToWhitelist_Click(sender, args)` | 菜单 | `void` | 将选中行 exe 加入白名单 |
| `AddRuleFromSelection(whitelist)` | bool | `void` | 内部：从选中日志行提取 exe 并加规则 |

### 匿名命名空间工具函数

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `ReadAllText(path)` | 文件路径 | `std::wstring` | 读整个文件为宽串 |
| `GetFileTime(path)` | 文件路径 | `uint64_t` | 获取文件最后修改时间（FILETIME→uint64） |
| `ReplaceAll(s, from, to)` | 串、from、to | `void` | 全局替换（in-place） |
| `TranslateTokenName(s, en, zh)` | 串、英文、中文 | `void` | 替换启发式得分项名为中文 |
| `TranslateLogLine(raw)` | 原始日志行 | `std::wstring` | 整行翻译为中文显示 |
