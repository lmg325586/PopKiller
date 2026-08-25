# 函数输入输出参考

## AppSettings.h（配置读写）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `IniPath()` | 无 | `std::wstring` | exe 同目录 `winui.ini` 全路径 |
| `Flush()` | 无 | `void` | 刷写 ini 缓存到磁盘 |
| `ReadInt(section, key, def)` | 段、键、默认值 | `int` | 读 ini 整数值 |
| `WriteInt(section, key, value)` | 段、键、值 | `void` | 写 ini 并 Flush |
| `ReadString(section, key, def)` | 段、键、默认串（可选） | `std::wstring` | 栈缓冲 4096 字符读取 |
| `WriteString(section, key, value)` | 段、键、值 | `void` | 写 ini 并 Flush |
| `DeleteKey(section, key)` | 段、键 | `void` | 删除 ini 键并 Flush |

## RuleTypes.h（规则类型定义）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `Lower(s)` | 原串 | `std::wstring` | 小写化副本（`towlower` 逐字符转换） |
| `RuleKey(r)` | `Rule` 常量引用 | `std::wstring` | 生成规则唯一键，格式 `名单|字段|模式|内容`（如 `W|exe|contains|explorer.exe`），用于去重与删除记忆 |

枚举：
- `RuleField { Exe, Path, Title, Class }`
- `MatchMode { Contains, Exact, Wildcard }`

结构体：`Rule { isWhitelist, field, mode, pattern, fromCommunity }`
- `fromCommunity`（`bool`，默认 `false`）：标记该规则是否来自社区规则库

## RuleStorage.h（规则 JSON 读写存储）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `RulesPath()` | 无 | `std::wstring` | exe 同目录 `rules.json` 全路径 |
| `WStringToUtf8(wstr)` | 宽串 | `std::string` | UTF-16 → UTF-8 转换 |
| `Utf8ToWString(str)` | UTF-8 串 | `std::wstring` | UTF-8 → UTF-16 转换 |
| `ReadFileToUtf8String(path, out)` | 文件路径、输出串（out） | `bool` | 读文件到 UTF-8 串，自动跳过 BOM；文件不存在返回 false |
| `WriteUtf8StringToFile(path, text)` | 文件路径、UTF-8 文本 | `bool` | 覆盖写文件（`CREATE_ALWAYS`） |
| `ParseRuleLine(line, r)` | 四段式串、`Rule`（out） | `bool` | 解析旧格式 `B:exe:contains:xxx` 或简写 `exe:xxx`，pattern 为空返回 false |
| `ParseRulesFromJsonString(utf8_text, out)` | JSON 文本、规则向量（out） | `bool` | 解析 `rules.json` 格式，读取 `list/field/mode/pattern/source` 字段；`source=="community"` 时置 `fromCommunity=true` |
| `LoadRulesJson(out, removedOut)` | 规则向量（out）、删除键列表（out） | `bool` | 从 `rules.json` 加载规则 + `communityRemoved` 数组；解析失败返回 false |
| `SerializeRules(rules, removed)` | 规则向量、删除键列表 | `std::string` | 序列化为 JSON 字符串（`version:1`，4 空格缩进）；社区规则带 `"source":"community"`；`communityRemoved` 数组一并写入 |
| `SaveRulesJson(rules, removed)` | 规则向量、删除键列表 | `bool` | 保存规则到 `rules.json`，内部调用 `SerializeRules` + `WriteUtf8StringToFile` |
| `EnsureDefaultRules()` | 无 | `void` | 首运行（`rules.json` 不存在）写 4 条默认规则并保存 |

## LabelStorage.h（日志标注与训练样本导出）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `LabelsPath()` | 无 | `std::wstring` | exe 同目录 `labels.json` 全路径 |
| `ExtractField(s, key)` | 日志行、字段名（如 `action=`） | `std::wstring` | 从日志行提取字段值，以 ` | ` 为分隔符；未找到返回空串 |
| `ParseLine(line)` | 日志行 | `Sample` | 解析日志行为 Sample 结构体：提取 action/reason/title/class/exe/score（从 `heuristic(N)`）/raw（从 `raw=`，缺失则 17 个 `?`） |
| `Load(out)` | 标注 map（out） | `void` | 从 `labels.json` 加载标注数据，以日志原文为 key；解析失败静默返回 |
| `Save(m)` | 标注 map 常量引用 | `bool` | 保存标注到 `labels.json`（4 空格缩进），写入失败返回 false |
| `ExportJson(m)` | 标注 map 常量引用 | `std::string` | 导出为 `popkiller_training_samples` 格式 JSON 字符串（含 `version`/`type`/`samples`），用于训练模型 |

结构体：`Sample { label, line, action, reason, title, cls, exe, raw, score }`
- `label`：`popup`（弹窗）/ `notpopup`（误关）
- `line`：日志原文（作为 map 的 key）

## FilePicker.h（公共文件选择器）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `GetAppHwnd()` | 无 | `HWND` | 通过 `App::window` 获取主窗口原生句柄，失败返回 nullptr |
| `PickJsonFile(save)` | `bool`（true=保存，false=打开） | `std::wstring` | Win32 `GetSaveFileNameW`/`GetOpenFileNameW` 选择 JSON 文件；过滤器「JSON 文件\|*.json\|所有文件\|*.*」；保存时 `OFN_OVERWRITEPROMPT`，打开时 `OFN_FILEMUSTEXIST`；取消返回空串 |

依赖：`comdlg32.lib`

## AutoStart.h（开机自启动管理）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `DebugLog(msg)` | 日志消息 | `void` | 写调试日志到 exe 同目录 `autostart_debug.log`，格式 `HH:MM:SS user=xxx msg` |
| `GetExePathQuoted()` | 无 | `std::wstring` | 获取带双引号的当前 exe 完整路径（`"C:\path\winui.exe"`） |
| `IsEnabled()` | 无 | `bool` | 检查自启是否启用：Run 键存在 **且** StartupApproved 未被禁用（首字节非 0x01/0x03） |
| `EnableAutoStartup()` | 无 | `bool` | 写注册表 Run 键（`REG_SZ`，带引号 exe 路径）+ 清除 StartupApproved 禁用标记；写失败返回 false |
| `DisableAutoStartup()` | 无 | `bool` | 删除 Run 键值；删除成功或键不存在均返回 true |
| `SyncPath()` | 无 | `void` | 已启用时重新调用 `EnableAutoStartup()`，用于 exe 路径变化时更新注册表路径 |

常量：
- `RunKeyPath` = `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
- `ApprovedPath` = `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run`
- `ValueName` = `PopKiller`

依赖：`advapi32.lib`（注册表 API）

## PopupBlocker.h（拦截引擎）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `LogPath()` | 无 | `wstring` | `blocklog.txt` 全路径 |
| `WildcardMatch(str, pat)` | 目标串、模式串 | `bool` | 大小写不敏感，支持 `*`/`?` |
| `InitSelfExe()` | 无 | `void` | 设置 `SelfExe`（自身 exe 小写名） |
| `LooksLikePopup(hwnd)` | 窗口句柄 | `bool` | 有 owner / toolwindow / 不可调且无最小最大化 |
| `SaveRules(newRules)` | 规则向量 | `void` | 调用 `SaveRulesJson` 写入磁盘 + 加锁更新内存 `Rules` |
| `SyncFromSettings()` | 无 | `void` | 从 ini 刷新 `ForceBlock/HeuristicMode/HeuristicThreshold/VerboseLog/MLHeuristic`；调用 `EnsureDefaultRules`；从 `rules.json` 加载规则与 `communityRemoved` 到内存 |
| `FetchCommunityRulesAsync()` | 无 | `IAsyncAction`（co_await） | 异步从 GitHub raw 拉取 `community_rules.json`（带时间戳防缓存）；解析后标记 `fromCommunity=true`；跳过墓碑与已存在规则；新增则 `SaveRules`；完成后调用 `CommunityRulesFetchCallback(ok, msg)`，`msg` 为新增条数或错误信息 |
| `Start()` | 无 | `void` | `InitSelfExe` + 启动钩子线程（`Running` 置位，幂等） |
| `Stop()` | 无 | `void` | 发 `WM_QUIT` + join 线程（幂等） |
| `detail::GetProcessName(hwnd)` | 句柄 | `wstring` | 小写 exe 文件名 |
| `detail::GetProcessPath(hwnd)` | 句柄 | `wstring` | 小写完整进程路径 |
| `detail::IsProtected(hwnd)` | 句柄 | `bool` | 自身 + 系统核心进程（含 svchost/services/lsass/csrss）+ 常见浏览器白名单 |
| `detail::GetTitle(hwnd)` | 句柄 | `wstring` | 小写窗口标题（截断 256） |
| `detail::GetClass(hwnd)` | 句柄 | `wstring` | 小写窗口类名 |
| `detail::MatchRule(hwnd, r, exe&, path&, title&, cls&)` | 句柄、规则、4 个缓存串（in/out 惰性填充） | `bool` | 单条规则匹配 |
| `detail::Match(hwnd)` | 句柄 | `int` | 0=未命中，1=白名单，2=黑名单（白名单优先）；加锁复制 `Rules` 后遍历 |
| `detail::Log(s)` | 日志行内容 | `void` | 追加 UTF-8 带时间戳行；新文件或超过 1MB 时截断重写并写 BOM |
| `detail::WinEventProc(hook, idEvent, hwnd, idObject, idChild, idEventThread, idEventTime)` | WinEvent 参数 | `void` | 过滤→匹配→打分→日志→关闭/隐藏；启发式启用时日志追加 `raw=` 17位特征串（含 idle>5s / farFromMouse>300px）；`MLHeuristic` 启用时追加 `ml=Y/N` 预测结果；日志新增 `ev=SHOW/FG` 事件类型；黑名单起延迟 400ms 强杀线程（排除系统路径与 Program Files 路径） |
| `detail::ThreadMain(lp)` | 无（`lp` 未用） | `DWORD` | 挂 `EVENT_OBJECT_SHOW` + `EVENT_SYSTEM_FOREGROUND` 双钩子 + 消息循环 |

全局状态：`Rules`、`RulesMutex`、`CommunityRemoved`、`CommunityRulesFetchCallback`、`Running`、`ForceBlock`、`SelfExe`、`HeuristicMode`、`HeuristicThreshold`、`VerboseLog`、`MLHeuristic`、`EnabledChangedCallback`。

> 注：`Lower()` 与 `EnsureDefaultRules()` 已分别移至 `RuleTypes.h` 和 `RuleStorage.h`；`Rule` 结构体与枚举统一使用 `RuleTypes.h` 中的定义。

## HeuristicScorer.h（启发式打分）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `detail::Lower(s)` | 原串 | `wstring` | 小写化（独立副本，避免循环依赖） |
| `detail::GetProcessPath(hwnd)` | 句柄 | `wstring` | 小写进程路径 |
| `detail::GetTitle(hwnd)` | 句柄 | `wstring` | 小写窗口标题 |
| `detail::GetClass(hwnd)` | 句柄 | `wstring` | 小写窗口类名 |
| `DigitRatio(s)` | 串 | `float` | 数字字符占比 (0~1) |
| `HexRatio(s)` | 串 | `float` | 十六进制字符占比 (0~1) |
| `ProcessAgeSeconds(hwnd)` | 句柄 | `float` | 进程已运行秒数，失败返回 -1 |
| `IsFileSigned(path)` | 文件路径 | `bool` | `WinVerifyTrust` 验证；返回值等于 `ERROR_SUCCESS` 才视为有签名（链错误/无签名均返回 false） |
| `IsFileSignedCached(path)` | 文件路径 | `bool` | 带 `SigCache` + 互斥锁的签名查询 |
| `ExtractFeatures(hwnd)` | 窗口句柄 | `Features` | 提取 21 项特征（样式/尺寸/标题/类名/路径/年龄） |
| `ScoreWindow(f, detail&)` | 特征、明细串（out） | `int` | ≥0 加权总分；基础设施类名/零尺寸窗口硬过滤返回 0，`detail` 为 `infra_class_skip`/`zero_size_skip`/含 `no_path` |

结构体：`Weights`（21 项权重，含 `unsignedExe`/`unsignedUserDir`/`signedExe`/`youngProcess`/`clsHex`）、`Features`（全部特征字段）。

全局状态：`g_w`（权重表单例）、`SigMx`（缓存锁）、`SigCache`（签名结果缓存）。

> 基础设施类名跳过列表：`consolewindowclass`、`pseudoconsolewindow`、`hwndwrapper[...]`、`tooltip`、`tooltip_*`、`msctfime ui`、`default ime`、`dragvisualwindow`、`#32768`、`shell_systemdialog`、`shell_systemdialogproxy`、`shell_systemdim`。

## HeuristicML.h（静态机器学习识别）

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `ToLower(s)` | 原串 | `wstring` | 小写化（独立副本） |
| `GetTitle(hwnd)` | 句柄 | `wstring` | 小写窗口标题（截断 256） |
| `GetClass(hwnd)` | 句柄 | `wstring` | 小写窗口类名 |
| `GetProcessName(hwnd)` | 句柄 | `wstring` | 小写进程名（从完整路径提取文件名） |
| `GetInstance()` | 无 | `MLEngine&` | 返回机器学习引擎单例引用 |

`MLEngine` 类成员：

| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `MLEngine()` | 无 | 构造 | 初始化 `Ort::Env`（日志级别 WARNING） |
| `Init()` | 无 | `bool` | 从 exe 同目录 `StaticML\` 加载 `popup_rf.onnx`（随机森林）与 `popup_lr.onnx`（逻辑回归）；文件不存在或加载异常返回 false；已初始化则直接返回 true |
| `RunSession(session, features)` | ONNX Session 指针、23维特征数组 | `bool` | 运行单模型推理；输出 label==1 时返回 true；异常时返回 false |
| `Predict(hwnd, evTime)` | 窗口句柄、事件时间戳 | `bool` | 主预测：`ExtractFeatures` 提取特征→构建 23 维输入数组（含 idle>5s / farFromMouse>300px）→双模型分别推理→**随机森林与逻辑回归同时预测为弹窗**才返回 true（双模型投票降误报）；模型未加载返回 false |

23 维输入特征（顺序）：

| 索引 | 特征 | 类型 | 说明 |
|---|---|---|---|
| 0 | `hasOwner` | 0/1 | 窗口有所有者 |
| 1 | `toolWin` | 0/1 | 工具窗口样式 |
| 2 | `topmost` | 0/1 | 置顶窗口 |
| 3 | `noActivate` | 0/1 | 不激活窗口 |
| 4 | `resizable` | 0/1 | 可调整大小 |
| 5 | `hasMinMax` | 0/1 | 有最小/最大化按钮 |
| 6 | `captionSysmenu` | 0/1 | 有标题栏和系统菜单 |
| 7 | `titleEmpty` | 0/1 | 标题为空 |
| 8 | `smallWindow` | 0/1 | 小于 400×300 |
| 9 | `largeWindow` | 0/1 | 大于 800×600 |
| 10 | `pathTemp` | 0/1 | 路径含 Temp 目录 |
| 11 | `pathRoaming` | 0/1 | 路径含 AppData\Roaming |
| 12 | `clsHexRatio` | 0/1 | 类名十六进制占比 > 0.8 |
| 13 | `youngProcess` | 0/1 | 进程年龄 < 120 秒 |
| 14 | `unsigned` | 0/1 | 文件未签名 |
| 15 | `idle>5s` | 0/1 | 用户空闲超过 5 秒（`GetLastInputInfo`） |
| 16 | `farFromMouse` | 0/1 | 窗口出现时离鼠标光标超过 300 像素 |
| 17 | `titleLen` | 连续值 | 标题字符长度 |
| 18 | `adKwHits` | 连续值 | 广告关键词命中数（14 个关键词） |
| 19 | `isGoodExe` | 0/1 | 进程在白名单列表中（27 个常见正常软件） |
| 20 | `isWidgetWin` | 0/1 | 类名含 `widgetwin`（Chromium 系正常窗口） |
| 21 | `isDlg32770` | 0/1 | 类名是 `#32770`（标准对话框） |
| 22 | `exeDigitRatio` | 连续值 | exe 文件名数字字符占比 |

全局常量：
- `AD_KEYWORDS`：14 个广告关键词（广告、优惠、促销、免费、中奖、礼包、热点、速看、推荐、清理、加速、升级、弹窗、资讯）
- `GOOD_EXES`：27 个白名单进程（devenv、code、chrome、msedge、firefox、windowsterminal、explorer、wechat、weixin、qq、dingtalk、tim、notepad、notepad++、everything、snipaste、listary、steamwebhelper、steam、qbittorrent、rvrvpngui、mixline、mixline.ui、oopz、translucenttb、hyp、svchost）

依赖：`Microsoft.ML.OnnxRuntime` NuGet 包（`onnxruntime_cxx_api.h`）

模型文件：`StaticML\popup_rf.onnx`（随机森林，~202KB）、`StaticML\popup_lr.onnx`（逻辑回归，~1KB）

> 当前为仅记录模式：预测结果写入日志 `ml=Y/N`，不执行拦截。

## WindowPicker.h（窗口拾取）
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `detail::FrameProc(h, msg, wp, lp)` | 窗口消息 | `LRESULT` | 蓝框窗口过程，转交 `DefWindowProc` |
| `detail::EnsureFrame()` | 无 | `void` | 创建分层穿透蓝框窗口 |
| `detail::PaintFrame()` | 无 | `void` | `UpdateLayeredWindow` 绘制蓝色边框位图 |
| `detail::MoveFrame()` | 无 | `void` | 蓝框位置贴合 `Target` 窗口 |
| `detail::EnsureOverlay()` | 无 | `void` | 创建全屏半透明遮罩（alpha=10） |
| `detail::Teardown(commit)` | 是否提交结果 | `void` | 隐藏窗口、释放捕获/热键；commit 时经 `GA_ROOT` 填 `PickResult` 回调 |
| `detail::OverlayProc(h, msg, wp, lp)` | 窗口消息 | `LRESULT` | 鼠标移动高亮 / 左键提交 / 右键·ESC 取消 |
| `Start(mainHwnd, cb)` | 主窗口句柄、结果回调 | `void` | 显示遮罩、注册 ESC 热键、`SetCapture` |
| `Cancel()` | 无 | `void` | `Teardown(false)` 取消拾取 |

输出结构：`PickResult { exe, processPath, title, className }`。

## TrayIcon.h（系统托盘）
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `GetIcon()` | 无 | `HICON` | 从 exe 提取图标，失败回退 `IDI_APPLICATION` |
| `Add()` | 无 | `void` | 添加托盘图标（幂等，`Visible` 守卫） |
| `Remove()` | 无 | `void` | 删除托盘图标（幂等） |
| `HideToTray()` | 无 | `void` | 隐藏窗口 + 加托盘图标 + 回调 + `SetProcessWorkingSetSize` 修剪内存 |
| `Restore()` | 无 | `void` | 删除托盘图标 + 显示窗口 + 回调 + 置前台 |
| `ToggleBlocker()` | 无 | `void` | 切换拦截启停 + 写 ini + 触发 `EnabledChangedCallback` |
| `Handle(msg, wp, lp)` | 窗口消息 | `bool` | 消费 `SC_MINIMIZE`（最小化到托盘）和 `WM_TRAYICON`（左键还原/右键菜单），返回是否已消费 |
| `Init(hwnd)` | 主窗口句柄 | `void` | 绑定托盘宿主窗口 |

常量：`WM_TRAYICON`、`IDM_SHOW`、`IDM_EXIT`、`IDM_TOGGLE`。
全局状态：`Hwnd`、`Visible`、`OnHideToTray`、`OnRestoreFromTray`。

## AppTheme.h（主题与标题栏）
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `ApplyTitleBar(titleBar)` | `AppWindowTitleBar` 对象 | `void` | 按 `Index` 设背景（Mica=透明 / 否则白）、前景与按钮各态颜色，同步 `TitleBarElement` 背景 |

全局状态：`Index`（0=普通 / 1=Mica）、`TitleBarElement`（自定义标题栏元素引用）。

## RuleIOPage.xaml.cpp（规则导入导出页）
匿名命名空间：
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `GetAppHwnd()` | 无 | `HWND` | 通过 `App::window` 获取主窗口原生句柄，失败返回 nullptr |
| `PickJsonFile(save)` | `bool`（true=保存，false=打开） | `wstring` | Win32 `GetSaveFileNameW`/`GetOpenFileNameW` 选择 JSON 文件；过滤器「JSON 文件\|*.json\|所有文件\|*.*」；保存时 `OFN_OVERWRITEPROMPT`，打开时 `OFN_FILEMUSTEXIST`；取消返回空串 |

成员：
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `RuleIOPage::RuleIOPage()` | 无 | 无（构造） | `InitializeComponent` |
| `BackButton_Click(sender, e)` | 点击事件 | `void` | `Frame().CanGoBack()` 时 `GoBack()` 返回拦截规则页 |
| `ExportButton_Click(sender, e)` | 点击事件 | `void` | 选保存路径→读两个 CheckBox→加锁复制 `Rules`/`CommunityRemoved`→按选项过滤社区规则与墓碑→`WriteUtf8StringToFile(path, SerializeRules(...))`→结果文本显示导出条数 |
| `ImportButton_Click(sender, e)` | 点击事件 | `void` | 选文件→`ReadFileToUtf8String`→`ParseRulesFromJsonString`→加锁复制现有规则→遍历导入规则（强制 `fromCommunity=false`，`RuleKey` 去重）→有新增则 `SaveRules`→结果文本显示新增/跳过条数 |

## MainWindow.xaml.cpp（主窗口）
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `MinSizeSubclass(h, msg, wp, lp, id, ref)` | 窗口消息 | `LRESULT` | 子类过程：先交 `TrayIcon::Handle`，再处理 `WM_GETMINMAXINFO` 限制最小尺寸 640×480（DPI 感知） |
| `MainWindow::MainWindow()` | 无 | 无（构造） | 初始化标题栏/主题/托盘子类/自动启动拦截引擎/导航默认页 |
| `NavView_SelectionChanged(sender, args)` | 导航选中事件 | `void` | 根据 `Tag` 导航到 Home/Blocker/BlockLog/Settings 页 |
| `NavigateToTag(tag)` | Tag 字符串 | `void` | 按 Tag 选中对应 `NavigationViewItem` |

## HomePage.xaml.cpp（首页）
匿名命名空间：
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `ReadRegString(key, value)` | 注册表句柄、值名 | `wstring` | `RegGetValueW` 读字符串，512 字符缓冲 |
| `EditionName(id)` | EditionID 串 | `wstring` | 映射为中文版本名（专业版/家庭版/企业版等） |
| `PlaceEllipse(el, x, y)` | 椭圆元素、中心坐标 | `void` | 设置 Canvas `Left`/`Top` 使椭圆居中于 (x,y) |
| `ClipToSelf(el)` | FrameworkElement | `void` | 用 `RectangleGeometry` 裁剪到自身尺寸 |
| `UpdateGlow(card, canvas, layers, e)` | 卡片、画布、椭圆组、指针事件 | `void` | 计算指针距卡片距离，100px 内渐显辉光并定位椭圆 |

成员：
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `HomePage::HomePage()` | 无 | 无（构造） | 注册 SizeChanged 裁剪 + 读注册表显示 Windows 版本/版本/注册人/组织 |
| `RootPointerMoved(sender, e)` | 指针移动事件 | `void` | 同时更新两张卡片的辉光 |
| `RootPointerExited(sender, e)` | 指针离开事件 | `void` | 两张卡片辉光透明度归零 |
| `GoToBlocker_Tapped(sender, e)` | 点击事件 | `void` | 调用主窗口 `NavigateToTag(L"Blocker")` 跳转拦截页 |

## PopupBlockerPage.xaml.cpp（拦截规则页）
匿名命名空间：`ListTypeKey/Label`、`FieldKey/Label`、`MatchModeKey/Label`（下标↔字符串映射）。

成员：
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `PopupBlockerPage::PopupBlockerPage()` | 无 | 无（构造） | `EnsureDefaultRules` + 初始化总开关 + 注册 `EnabledChangedCallback` 与 `CommunityRulesFetchCallback`（DispatcherQueue 回 UI 线程）+ `SyncFromSettings` + `ReloadRulesFromEngine` + 初始化社区规则开关 + 触发首次拉取 |
| `ToEngineRule(it)` | `RuleItem` 常量引用 | `PopupBlocker::Rule` | 将 UI 层 RuleItem 转换为引擎 Rule（含 `fromCommunity`） |
| `ReloadRulesFromEngine()` | 无 | `void` | 加锁从引擎复制 `Rules` 到 `m_rules`；`stable_partition` 本地规则在前、社区规则在后；`RefreshList` |
| `RefreshList()` | 无 | `void` | 清空列表，按 `m_searchText` 过滤后重建；社区规则前缀 `[社区] `；本地与社区分组显示；更新 `m_visibleIndex` 映射 |
| `Save()` | 无 | `void` | 将 `m_rules` 转为引擎 Rule 向量，调用 `PopupBlocker::SaveRules` 写入磁盘并同步内存 |
| `EnableToggle_Toggled(sender, e)` | 开关事件 | `void` | 写 `Enabled`，Start/Stop 引擎 |
| `CommunityRulesToggle_Toggled(sender, e)` | 开关事件 | `void` | `m_initialized` 守卫；写 `CommunityRulesEnabled`；开→状态文本「正在拉取…」+ 隐藏重试 + `FetchCommunityRulesAsync`；关→清空状态 |
| `UpdateCommunityStatus(ok, msg)` | 成功标志、消息串 | `void` | 私有函数：成功→状态文本「社区规则已更新，新增 X 条」+ 灰色 + 隐藏重试 + `ReloadRulesFromEngine`；失败→橙色错误文本 + 显示重试按钮 |
| `RetryFetchButton_Click(sender, e)` | 点击事件 | `void` | 状态文本「正在拉取…」+ 隐藏重试 + `FetchCommunityRulesAsync` |
| `EditRule_Click(sender, e)` | 点击事件 | `void` | 经 `m_visibleIndex` 映射到真实下标；将规则回填到 ListType/RuleType/MatchMode/PatternInput；置 `m_editingIndex`；按钮文字变「保存修改」 |
| `AddRule_Click(sender, e)` | 点击事件 | `void` | 读输入→冲突检测（跳过当前编辑项）→编辑模式：修改原规则（社区规则旧键记入 `CommunityRemoved`，新规则 `fromCommunity=false`），重置按钮；新增模式：插入到 `m_rules` 开头→清空输入→`Save`→`RefreshList`；冲突时橙色警告 |
| `DeleteRule_Click(sender, e)` | 点击事件 | `void` | 经 `m_visibleIndex` 映射到真实下标；社区规则键记入 `CommunityRemoved`；`m_rules.erase`→`Save`→`RefreshList` |
| `Pick_Click(sender, e)` | 点击事件 | `void` | 获取主窗口 HWND→启动 `WindowPicker::Start`，回调按当前 RuleType 选择填充 exe/path/title/class，PickInfo 显示全部四项详情 |
| `SearchInput_TextChanged(sender, e)` | 文本变更事件 | `void` | 更新 `m_searchText`（小写）→`RefreshList` |
| `OpenIO_Click(sender, e)` | 点击事件 | `void` | `Frame().Navigate(RuleIOPage)` 跳转到导入导出页面 |

私有成员：
- `m_initialized`（`bool`）：初始化守卫，防止构造期间事件触发
- `m_editingIndex`（`int`，默认 -1）：当前编辑的规则在 `m_rules` 中的下标，-1 表示新增模式
- `m_rules`（`vector<RuleItem>`）：UI 层规则缓存
- `m_searchText`（`wstring`）：搜索关键词（小写）
- `m_visibleIndex`（`vector<size_t>`）：列表显示下标到 `m_rules` 下标的映射

`RuleItem` 结构体：`{ listType, fieldType, matchMode, pattern, fromCommunity }`。

## SettingsPage.xaml.cpp（设置页）
匿名命名空间：`IndexToMode(idx)`、`ModeToIndex(mode)`（下拉下标↔启发式模式映射，下标 1=自动拦截/2=仅记录）。

成员：
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `SettingsPage::SettingsPage()` | 无 | 无（构造） | 读 ini 初始化 VerboseLog/HeuristicMode/Theme/ForceBlock/MLHeuristic，读 `AutoStart::IsEnabled()` 初始化自启开关，`m_initialized` 守卫 |
| `LicenseLink_Click(sender, e)` | 点击事件 | `void` | 导航到 LicensePage |
| `ThemeComboBox_SelectionChanged(sender, e)` | 选择事件 | `void` | 写 `UI/Material` + 切换 Mica 背景 + `ApplyTitleBar` |
| `ForceBlockToggle_Toggled(sender, e)` | 开关事件 | `void` | 写 `ForceBlock` + 同步 `PopupBlocker::ForceBlock` |
| `HeuristicModeCombo_SelectionChanged(sender, e)` | 选择事件 | `void` | 下标→模式映射，写 ini + `SyncFromSettings` |
| `MLHeuristicToggle_Toggled(sender, e)` | 开关事件 | `void` | 写 `Blocker/MLHeuristic` + 同步 `PopupBlocker::MLHeuristic`；开启时调用 `HeuristicML::GetInstance().Init()` 加载模型 |
| `VerboseLogToggle_Toggled(sender, e)` | 开关事件 | `void` | 写 `VerboseLog` + `SyncFromSettings` |
| `AutoStartToggle_Toggled(sender, e)` | 开关事件 | `void` | 开→`AutoStart::EnableAutoStartup()`，关→`AutoStart::DisableAutoStartup()`；操作失败时回滚开关状态 |

## BlockLogPage.xaml.cpp（拦截日志页）
匿名命名空间：
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `ReadLogText()` | 无 | `wstring` | 读 `blocklog.txt` 全文，去 BOM，UTF-8 转宽串 |
| `LogWriteTime()` | 无 | `uint64_t` | 日志文件最后写时间（FILETIME 合并），不存在返回 0 |
| `GetRawLine(displayText)` | 显示文本 | `wstring` | 去除标注前缀 `[弹窗] ` / `[误关] `，返回原始日志行 |

成员：
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `BlockLogPage::BlockLogPage()` | 无 | 无（构造） | `SampleLabels::Load(m_labels)` + `Load` + 创建 1s `DispatcherTimer` + Unloaded 停表 |
| `Load()` | 无 | `void` | 清空列表，`SampleLabels::Load` 刷新标注；按行切分后倒序追加；已标注行前缀 `[弹窗] ` / `[误关] `；记录 `m_lastWrite` |
| `Timer_Tick(sender, e)` | 定时器事件 | `void` | 文件写时间变化才触发 `Load` |
| `Refresh_Click(sender, e)` | 点击事件 | `void` | 手动 `Load` |
| `Clear_Click(sender, e)` | 点击事件 | `void` | `"wb"` 截断日志文件 + `Load` |
| `LogItem_RightTapped(sender, e)` | 右键事件 | `void` | 记录选中项文本到 `m_selectedDisplayText`，供右键菜单使用 |
| `MarkPopup_Click(sender, e)` | 点击事件 | `void` | `ParseLine` 解析选中日志→置 `label=popup`→存入 `m_labels`→`SampleLabels::Save`→`Load` 刷新显示 |
| `MarkNotPopup_Click(sender, e)` | 点击事件 | `void` | `ParseLine` 解析选中日志→置 `label=notpopup`→存入 `m_labels`→`SampleLabels::Save`→`Load` 刷新显示 |
| `ExportSamples_Click(sender, e)` | 点击事件 | `void` | `FilePicker::PickJsonFile(true)` 选保存路径→`SampleLabels::ExportJson(m_labels)` 导出→`WriteUtf8StringToFile` 写入→MessageBox 提示成功/失败 |
| `AddToBlacklist_Click(sender, e)` | 点击事件 | `void` | 调用 `AddRuleFromSelection(false)` 添加黑名单规则 |
| `AddToWhitelist_Click(sender, e)` | 点击事件 | `void` | 调用 `AddRuleFromSelection(true)` 添加白名单规则 |
| `AddRuleFromSelection(whitelist)` | `bool`（true=白名单，false=黑名单） | `void` | 私有函数：`ParseLine` 解析选中日志→提取 exe→生成精确匹配规则→`RuleKey` 去重检测→`SaveRules`→MessageBox 提示结果；exe 为空时警告 |

私有成员：
- `m_labels`（`map<wstring, SampleLabels::Sample>`）：标注数据缓存（以日志原文为 key）
- `m_selectedDisplayText`（`wstring`）：右键选中的日志显示文本
- `m_lastWrite`（`uint64_t`）：上次检测的日志文件写时间
- `m_timer`（`DispatcherTimer`）：1 秒轮询定时器

## LicensePage.xaml.cpp（许可页）
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `LicensePage::LicensePage()` | 无 | 无（构造） | 设置 MIT 许可证全文到 `LicenseText` |
| `BackButton_Click(sender, e)` | 点击事件 | `void` | `Frame.GoBack()` 返回上一页 |

## App.xaml.cpp（应用入口 & 单实例）
匿名命名空间：
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `ActivateFirstInstance()` | 无 | `void` | 枚举所有顶层窗口，找到同名 exe 的其他进程窗口；发送 `WM_TRAYICON`(WM_LBUTTONDBLCLK) 触发恢复；Sleep 150ms 后 `AttachThreadInput` + `SetForegroundWindow` 前置 + `FlashWindow` 闪烁 |

成员：
| 函数 | 输入 | 输出 | 说明/副作用 |
|---|---|---|---|
| `App::App()` | 无 | 无（构造） | Debug 模式下注册 `UnhandledException` 回调触发 `__debugbreak` |
| `App::OnLaunched(e)` | 启动参数 | `void` | `FindOrRegisterForKey("PopKiller_Main")` 注册单实例；非当前实例→调用 `ActivateFirstInstance()` 激活已有窗口→`Exit()`；当前实例→创建 `MainWindow` 并 `Activate` |

全局变量：
- `window`（主窗口实例引用，供各页面获取 HWND 或操作窗口属性）
- `m_keyInstance`（`AppInstance` 单实例句柄）