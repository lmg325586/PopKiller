From 47e05a9b6f9177c636fe2f42cbc3a791677d3853 Mon Sep 17 00:00:00 2001
From: Docs Bot <docs-bot@local>
Date: Tue, 18 Aug 2026 17:57:34 +0800
Subject: [PATCH] =?UTF-8?q?docs:=20=E9=87=8D=E6=9E=84=E5=87=BD=E6=95=B0?=
 =?UTF-8?q?=E8=BE=93=E5=85=A5=E8=BE=93=E5=87=BA=E8=A1=A8=E4=B8=BA=20Markdo?=
 =?UTF-8?q?wn=20=E8=A1=A8=E6=A0=BC=E6=A0=BC=E5=BC=8F?=
MIME-Version: 1.0
Content-Type: text/plain; charset=UTF-8
Content-Transfer-Encoding: 8bit

- 全部模块统一为 函数/输入/输出/说明 四列表格
- 新增 TrayIcon.h、AppTheme.h、MainWindow、HomePage、LicensePage 等模块
- PopupBlockerPage 补充 SearchInput_TextChanged
- SettingsPage 补充 HeuristicModeCombo/VerboseLogToggle 事件
- 每个模块列出枚举/结构体/全局状态
- 修正 Match() 返回值、IsFileSigned 判定逻辑等描述
---
 winui/DOCUMENTATION.md | 282 ++++++++++++++++++++++++++++-------------
 1 file changed, 196 insertions(+), 86 deletions(-)

diff --git a/winui/DOCUMENTATION.md b/winui/DOCUMENTATION.md
index b6248b9..d282661 100644
--- a/winui/DOCUMENTATION.md
+++ b/winui/DOCUMENTATION.md
@@ -1,88 +1,198 @@
-﻿MainWindow.xaml.cpp
-LRESULT CALLBACK MinSizeSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) — 消息+参数 → LRESULT（托盘/最小尺寸拦截）
-MainWindow::MainWindow() — 无 → 无（构造）
-void NavView_SelectionChanged(NavigationView const&, NavigationViewSelectionChangedEventArgs const&) — 选中事件 → 导航页面
-void NavigateToTag(hstring const& tag) — Tag 字符串 → 选中对应导航项
-HomePage.xaml.cpp
-std::wstring ReadRegString(HKEY key, LPCWSTR value) — 注册表句柄+值名 → 字符串值
-std::wstring EditionName(std::wstring const& id) — EditionID → 中文版本名
-void PlaceEllipse(Shapes::Ellipse const& el, double x, double y) — 椭圆+中心坐标 → 设置 Canvas Left/Top
-void ClipToSelf(FrameworkElement const& el) — 元素 → 裁剪到自身矩形
-void UpdateGlow(FrameworkElement const& card, UIElement const& canvas, std::initializer_list<Shapes::Ellipse> layers, PointerRoutedEventArgs const&) — 卡片+画布+椭圆组+指针事件 → 设置透明度与椭圆位置
-HomePage::HomePage() — 无 → 无
-void RootPointerMoved(IInspectable const&, PointerRoutedEventArgs const&) — 指针事件 → 更新两卡辉光
-void RootPointerExited(IInspectable const&, PointerRoutedEventArgs const&) — 无有效输入 → 辉光归零
-void GoToBlocker_Tapped(IInspectable const&, TappedRoutedEventArgs const&) — 点击 → 跳转拦截页
-SettingsPage.xaml.cpp
-SettingsPage::SettingsPage() — 无 → 无
-void LicenseLink_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → 导航许可页
-void ThemeComboBox_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) — 选择事件 → 写配置+切材质/标题栏
-void ForceBlockToggle_Toggled(IInspectable const&, RoutedEventArgs const&) — 开关事件 → 写配置+更新 PopupBlocker::ForceBlock
-PopupBlockerPage.xaml.cpp
-PopupBlockerPage::PopupBlockerPage() — 无 → 无
-void EnableToggle_Toggled(IInspectable const&, RoutedEventArgs const&) — 开关事件 → 启/停引擎+写配置
-void AddRule_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → 新增规则并持久化
-void DeleteRule_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → 删除规则并持久化
-void Pick_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → 启动拾取器，回调填充输入框
-void RefreshList()（私有）— 无 → 渲染规则列表
-void Save()（私有）— 无 → 规则写回 ini
-PopupBlocker.h
-std::wstring LogPath() — 无 → 日志文件全路径
-std::wstring Lower(std::wstring s) — 字符串 → 小写副本
-void InitSelfExe() — 无 → 设置 SelfExe
-bool LooksLikePopup(HWND hwnd) — 窗口句柄 → 是否弹窗形态
-void EnsureDefaultRules() — 无 → 首跑写默认规则
-void SyncFromSettings() — 无 → 刷新 ForceBlock+Rules
-void Start() / void Stop() — 无 → 起/停钩子线程
-detail::void Log(std::wstring const& s) — 日志内容 → 追加一行到文件
-detail::std::wstring GetProcessName(HWND hwnd) — 句柄 → 小写进程名
-detail::bool IsProtected(HWND hwnd) — 句柄 → 是否白名单/自身
-detail::std::wstring GetTitle(HWND hwnd) / GetClass(HWND hwnd) — 句柄 → 小写标题/类名
-detail::int Match(HWND hwnd) — 句柄 → 命中规则下标，-1 未命中
-detail::void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) — 系统事件 → 判定并关窗+记日志
-detail::DWORD WINAPI ThreadMain(LPVOID) — 无 → 钩子消息循环线程体
-WindowPicker.h
-detail::LRESULT CALLBACK FrameProc(HWND, UINT, WPARAM, LPARAM) — 消息 → LRESULT
-detail::void EnsureFrame() — 无 → 创建蓝框窗
-detail::void PaintFrame() — 无 → 重画蓝边位图
-detail::void MoveFrame() — 无 → 蓝框跟随 Target
-detail::void EnsureOverlay() — 无 → 创建全屏拾取窗
-detail::void Teardown(bool commit) — 是否提交 → 隐藏窗口，提交则回调结果
-detail::LRESULT CALLBACK OverlayProc(HWND, UINT, WPARAM, LPARAM) — 消息 → 命中/选取/取消
-void Start(HWND mainHwnd, std::function<void(PickResult)> cb) — 主窗口+回调 → 进入拾取
-void Cancel() — 无 → 退出拾取
-TrayIcon.h
-HICON GetIcon() — 无 → exe 图标句柄
-void Add() / void Remove() — 无 → 增/删托盘图标
-void HideToTray() — 无 → 隐藏窗口+加图标+回调+修剪内存
-void Restore() — 无 → 显示窗口+删图标+回调
-void ToggleBlocker() — 无 → 切换拦截启停+写配置+触发回调
-bool Handle(UINT msg, WPARAM wp, LPARAM lp) — 消息 → 是否已消费
-void Init(HWND hwnd) — 主窗口句柄 → 绑定托盘宿主
-AppSettings.h
-std::wstring IniPath() — 无 → exe 同目录 winui.ini 全路径
-void Flush() — 无 → 刷写 ini 到磁盘
-int ReadInt(const wchar_t* section, const wchar_t* key, int def) — 节/键/默认值 → int
-void WriteInt(const wchar_t* section, const wchar_t* key, int value) — 节/键/值 → 写 ini 并 Flush
-std::wstring ReadString(const wchar_t* section, const wchar_t* key, const std::wstring& def = L"") — 节/键/默认串(可选) → 字符串
-void WriteString(const wchar_t* section, const wchar_t* key, const std::wstring& value) — 节/键/值 → 写 ini 并 Flush
-void DeleteKey(const wchar_t* section, const wchar_t* key) — 节/键 → 删除该键
-AppTheme.h
-inline int32_t Index{ 0 } — 材质索引变量（0 普通 / 1 Mica）
-inline winrt::Microsoft::UI::Xaml::Controls::Panel TitleBarElement{ nullptr } — 自定义标题栏元素变量
-void ApplyTitleBar(winrt::Microsoft::UI::Windowing::AppTitleBar const& titleBar) — 输入：标题栏对象 → 无返回值；按 Index 设背景（Mica=透明 / 否则白）、前景与按钮各态颜色（黑/灰/LightGray），并同步 TitleBarElement 背景
-BlockLogPage.xaml.cpp
+﻿# 函数输入输出参考
+> 基于当前 `master` 分支代码整理。
+
+## AppSettings.h（配置读写）
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `IniPath()` | 无 | `std::wstring` | exe 同目录 `winui.ini` 全路径 |
+| `Flush()` | 无 | `void` | 刷写 ini 缓存到磁盘 |
+| `ReadInt(section, key, def)` | 段、键、默认值 | `int` | 读 ini 整数值 |
+| `WriteInt(section, key, value)` | 段、键、值 | `void` | 写 ini 并 Flush |
+| `ReadString(section, key, def)` | 段、键、默认串（可选） | `std::wstring` | 栈缓冲 4096 字符读取 |
+| `WriteString(section, key, value)` | 段、键、值 | `void` | 写 ini 并 Flush |
+| `DeleteKey(section, key)` | 段、键 | `void` | 删除 ini 键并 Flush |
+
+## PopupBlocker.h（拦截引擎）
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `LogPath()` | 无 | `wstring` | `blocklog.txt` 全路径 |
+| `Lower(s)` | 原串 | `wstring` | 小写化副本 |
+| `WildcardMatch(str, pat)` | 目标串、模式串 | `bool` | 大小写不敏感，支持 `*`/`?` |
+| `InitSelfExe()` | 无 | `void` | 设置 `SelfExe`（自身 exe 小写名） |
+| `LooksLikePopup(hwnd)` | 窗口句柄 | `bool` | 有 owner / toolwindow / 不可调且无最小最大化 |
+| `EnsureDefaultRules()` | 无 | `void` | 首运行写默认规则（`Initialized` 标志守卫） |
+| `SyncFromSettings()` | 无 | `void` | 从 ini 刷新 `ForceBlock/HeuristicMode/HeuristicThreshold/VerboseLog/Rules` |
+| `Start()` | 无 | `void` | 启动钩子线程（`Running` 置位，幂等） |
+| `Stop()` | 无 | `void` | 发 `WM_QUIT` + join 线程（幂等） |
+| `detail::GetProcessName(hwnd)` | 句柄 | `wstring` | 小写 exe 文件名 |
+| `detail::GetProcessPath(hwnd)` | 句柄 | `wstring` | 小写完整进程路径 |
+| `detail::IsProtected(hwnd)` | 句柄 | `bool` | 自身 + 系统核心进程白名单 |
+| `detail::GetTitle(hwnd)` | 句柄 | `wstring` | 小写窗口标题（截断 256） |
+| `detail::GetClass(hwnd)` | 句柄 | `wstring` | 小写窗口类名 |
+| `detail::MatchRule(hwnd, r, exe&, path&, title&, cls&)` | 句柄、规则、4 个缓存串（in/out 惰性填充） | `bool` | 单条规则匹配 |
+| `detail::Match(hwnd)` | 句柄 | `int` | 0=未命中，1=白名单，2=黑名单（白名单优先） |
+| `detail::Log(s)` | 日志行内容 | `void` | 追加 UTF-8 带时间戳行；新文件写 BOM |
+| `detail::WinEventProc(hook, event, hwnd, idObject, idChild, thread, time)` | WinEvent 参数 | `void` | 过滤→匹配→打分→日志→关闭/隐藏；黑名单起延迟强杀线程 |
+| `detail::ThreadMain(lp)` | 无（`lp` 未用） | `DWORD` | 挂 `EVENT_OBJECT_SHOW` + `EVENT_SYSTEM_FOREGROUND` 双钩子 + 消息循环 |
+
+枚举：`RuleField { Exe, Path, Title, Class }`、`MatchMode { Contains, Exact, Wildcard }`。
+结构体：`Rule { isWhitelist, field, mode, pattern }`。
+全局状态：`Rules`、`RulesMutex`、`Running`、`ForceBlock`、`SelfExe`、`HeuristicMode`、`HeuristicThreshold`、`VerboseLog`、`EnabledChangedCallback`。
+
+## HeuristicScorer.h（启发式打分）
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `detail::Lower(s)` | 原串 | `wstring` | 小写化（独立副本，避免循环依赖） |
+| `detail::GetProcessPath(hwnd)` | 句柄 | `wstring` | 小写进程路径 |
+| `detail::GetTitle(hwnd)` | 句柄 | `wstring` | 小写窗口标题 |
+| `detail::GetClass(hwnd)` | 句柄 | `wstring` | 小写窗口类名 |
+| `DigitRatio(s)` | 串 | `float` | 数字字符占比 (0~1) |
+| `HexRatio(s)` | 串 | `float` | 十六进制字符占比 (0~1) |
+| `ProcessAgeSeconds(hwnd)` | 句柄 | `float` | 进程已运行秒数，失败返回 -1 |
+| `IsFileSigned(path)` | 文件路径 | `bool` | `WinVerifyTrust` 验证；非 `TRUST_E_NOSIGNATURE` 即视为有签名 |
+| `IsFileSignedCached(path)` | 文件路径 | `bool` | 带 `SigCache` + 互斥锁的签名查询 |
+| `ExtractFeatures(hwnd)` | 窗口句柄 | `Features` | 提取 21 项特征（样式/尺寸/标题/类名/路径/年龄） |
+| `ScoreWindow(f, detail&)` | 特征、明细串（out） | `int` | ≥0 加权总分；基础设施类名/零尺寸窗口硬过滤返回 0，`detail` 为 `infra_class_skip`/`zero_size_skip`/含 `no_path` |
+
+结构体：`Weights`（19 项权重）、`Features`（全部特征字段）。
+全局状态：`g_w`（权重表单例）、`SigMx`（缓存锁）、`SigCache`（签名结果缓存）。
+
+## WindowPicker.h（窗口拾取）
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `detail::FrameProc(h, msg, wp, lp)` | 窗口消息 | `LRESULT` | 蓝框窗口过程，转交 `DefWindowProc` |
+| `detail::EnsureFrame()` | 无 | `void` | 创建分层穿透蓝框窗口 |
+| `detail::PaintFrame()` | 无 | `void` | `UpdateLayeredWindow` 绘制蓝色边框位图 |
+| `detail::MoveFrame()` | 无 | `void` | 蓝框位置贴合 `Target` 窗口 |
+| `detail::EnsureOverlay()` | 无 | `void` | 创建全屏半透明遮罩（alpha=10） |
+| `detail::Teardown(commit)` | 是否提交结果 | `void` | 隐藏窗口、释放捕获/热键；commit 时经 `GA_ROOT` 填 `PickResult` 回调 |
+| `detail::OverlayProc(h, msg, wp, lp)` | 窗口消息 | `LRESULT` | 鼠标移动高亮 / 左键提交 / 右键·ESC 取消 |
+| `Start(mainHwnd, cb)` | 主窗口句柄、结果回调 | `void` | 显示遮罩、注册 ESC 热键、`SetCapture` |
+| `Cancel()` | 无 | `void` | `Teardown(false)` 取消拾取 |
+
+输出结构：`PickResult { exe, processPath, title, className }`。
+
+## TrayIcon.h（系统托盘）
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `GetIcon()` | 无 | `HICON` | 从 exe 提取图标，失败回退 `IDI_APPLICATION` |
+| `Add()` | 无 | `void` | 添加托盘图标（幂等，`Visible` 守卫） |
+| `Remove()` | 无 | `void` | 删除托盘图标（幂等） |
+| `HideToTray()` | 无 | `void` | 隐藏窗口 + 加托盘图标 + 回调 + `SetProcessWorkingSetSize` 修剪内存 |
+| `Restore()` | 无 | `void` | 删除托盘图标 + 显示窗口 + 回调 + 置前台 |
+| `ToggleBlocker()` | 无 | `void` | 切换拦截启停 + 写 ini + 触发 `EnabledChangedCallback` |
+| `Handle(msg, wp, lp)` | 窗口消息 | `bool` | 消费 `SC_MINIMIZE`（最小化到托盘）和 `WM_TRAYICON`（左键还原/右键菜单），返回是否已消费 |
+| `Init(hwnd)` | 主窗口句柄 | `void` | 绑定托盘宿主窗口 |
+
+常量：`WM_TRAYICON`、`IDM_SHOW`、`IDM_EXIT`、`IDM_TOGGLE`。
+全局状态：`Hwnd`、`Visible`、`OnHideToTray`、`OnRestoreFromTray`。
+
+## AppTheme.h（主题与标题栏）
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `ApplyTitleBar(titleBar)` | `AppWindowTitleBar` 对象 | `void` | 按 `Index` 设背景（Mica=透明 / 否则白）、前景与按钮各态颜色，同步 `TitleBarElement` 背景 |
+
+全局状态：`Index`（0=普通 / 1=Mica）、`TitleBarElement`（自定义标题栏元素引用）。
+
+## MainWindow.xaml.cpp（主窗口）
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `MinSizeSubclass(h, msg, wp, lp, id, ref)` | 窗口消息 | `LRESULT` | 子类过程：先交 `TrayIcon::Handle`，再处理 `WM_GETMINMAXINFO` 限制最小尺寸 640×480（DPI 感知） |
+| `MainWindow::MainWindow()` | 无 | 无（构造） | 初始化标题栏/主题/托盘子类/自动启动拦截引擎/导航默认页 |
+| `NavView_SelectionChanged(sender, args)` | 导航选中事件 | `void` | 根据 `Tag` 导航到 Home/Blocker/BlockLog/Settings 页 |
+| `NavigateToTag(tag)` | Tag 字符串 | `void` | 按 Tag 选中对应 `NavigationViewItem` |
+
+## HomePage.xaml.cpp（首页）
+
 匿名命名空间：
-std::wstring ReadLogText() — 无 → 读 blocklog.txt 全文，去 BOM，UTF-8 转宽串
-uint64_t LogWriteTime() — 无 → 日志文件最后写时间（FILETIME 合并为 uint64），不存在返回 0
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `ReadRegString(key, value)` | 注册表句柄、值名 | `wstring` | `RegGetValueW` 读字符串，512 字符缓冲 |
+| `EditionName(id)` | EditionID 串 | `wstring` | 映射为中文版本名（专业版/家庭版/企业版等） |
+| `PlaceEllipse(el, x, y)` | 椭圆元素、中心坐标 | `void` | 设置 Canvas `Left`/`Top` 使椭圆居中于 (x,y) |
+| `ClipToSelf(el)` | FrameworkElement | `void` | 用 `RectangleGeometry` 裁剪到自身尺寸 |
+| `UpdateGlow(card, canvas, layers, e)` | 卡片、画布、椭圆组、指针事件 | `void` | 计算指针距卡片距离，100px 内渐显辉光并定位椭圆 |
+
 成员：
-BlockLogPage::BlockLogPage() — 无 → 无；首载 + 建 1s DispatcherTimer 挂 Timer_Tick + Unloaded 停表
-void Load() — 无 → 清空 LogList，按行切分倒序追加，记录 m_lastWrite
-void Timer_Tick(IInspectable const&, IInspectable const&) — 无有效输入 → 写时间变化才 Load()
-void Refresh_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → 手动 Load()
-void Clear_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → "wb" 截断日志文件 + Load()
-App.xaml.cpp
-App::App() — 无 → 无；在 Debug 模式下注册 UnhandledException 回调触发断点
-void OnLaunched(LaunchActivatedEventArgs const& e) — 启动参数 → 实例化 MainWindow 并调用 Activate() 显示窗口
-全局变量（配合 App.xaml.h）：
-inline Microsoft::UI::Xaml::Window window{ nullptr }; — 主窗口实例引用，供各页面（如 HomePage、SettingsPage）获取 HWND 或操作窗口属性
\ No newline at end of file
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `HomePage::HomePage()` | 无 | 无（构造） | 注册 SizeChanged 裁剪 + 读注册表显示 Windows 版本/版本/注册人/组织 |
+| `RootPointerMoved(sender, e)` | 指针移动事件 | `void` | 同时更新两张卡片的辉光 |
+| `RootPointerExited(sender, e)` | 指针离开事件 | `void` | 两张卡片辉光透明度归零 |
+| `GoToBlocker_Tapped(sender, e)` | 点击事件 | `void` | 调用主窗口 `NavigateToTag(L"Blocker")` 跳转拦截页 |
+
+## PopupBlockerPage.xaml.cpp（拦截规则页）
+
+匿名命名空间：`ListTypeKey/Label`、`FieldKey/Label`、`MatchModeKey/Label`（下标↔字符串映射）。
+
+成员：
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `PopupBlockerPage::PopupBlockerPage()` | 无 | 无（构造） | `EnsureDefaultRules` + 读规则到 `m_rules` + 初始化开关 + 注册 `EnabledChangedCallback` + `RefreshList` |
+| `RefreshList()` | 无 | `void` | 清空列表，按 `m_searchText` 过滤后重建，更新 `m_visibleIndex` 映射 |
+| `Save()` | 无 | `void` | 四段式写回 ini，清理多余的旧 `RuleN` 键 |
+| `EnableToggle_Toggled(sender, e)` | 开关事件 | `void` | 写 `Enabled`，Start/Stop 引擎 |
+| `AddRule_Click(sender, e)` | 点击事件 | `void` | 冲突检测→加规则→Save→Sync→刷新；冲突时橙色警告提示 |
+| `DeleteRule_Click(sender, e)` | 点击事件 | `void` | 经 `m_visibleIndex` 映射到真实下标后删除 |
+| `Pick_Click(sender, e)` | 点击事件 | `void` | 启动窗口拾取器，回调回填输入框与 PickInfo 详情 |
+| `SearchInput_TextChanged(sender, e)` | 文本变更事件 | `void` | 更新 `m_searchText`（小写）→ `RefreshList` |
+
+## SettingsPage.xaml.cpp（设置页）
+
+匿名命名空间：`IndexToMode(idx)`、`ModeToIndex(mode)`（下拉下标↔启发式模式映射，下标 1=自动拦截/2=仅记录）。
+
+成员：
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `SettingsPage::SettingsPage()` | 无 | 无（构造） | 读 ini 初始化 VerboseLog/HeuristicMode/Theme/ForceBlock，`m_initialized` 守卫 |
+| `LicenseLink_Click(sender, e)` | 点击事件 | `void` | 导航到 LicensePage |
+| `ThemeComboBox_SelectionChanged(sender, e)` | 选择事件 | `void` | 写 `UI/Material` + 切换 Mica 背景 + `ApplyTitleBar` |
+| `ForceBlockToggle_Toggled(sender, e)` | 开关事件 | `void` | 写 `ForceBlock` + 同步 `PopupBlocker::ForceBlock` |
+| `HeuristicModeCombo_SelectionChanged(sender, e)` | 选择事件 | `void` | 下标→模式映射，写 ini + `SyncFromSettings` |
+| `VerboseLogToggle_Toggled(sender, e)` | 开关事件 | `void` | 写 `VerboseLog` + `SyncFromSettings` |
+
+## BlockLogPage.xaml.cpp（拦截日志页）
+
+匿名命名空间：
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `ReadLogText()` | 无 | `wstring` | 读 `blocklog.txt` 全文，去 BOM，UTF-8 转宽串 |
+| `LogWriteTime()` | 无 | `uint64_t` | 日志文件最后写时间（FILETIME 合并），不存在返回 0 |
+
+成员：
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `BlockLogPage::BlockLogPage()` | 无 | 无（构造） | 首次 `Load` + 创建 1s `DispatcherTimer` + Unloaded 停表 |
+| `Load()` | 无 | `void` | 清空列表，按行切分后倒序追加，记录 `m_lastWrite` |
+| `Timer_Tick(sender, e)` | 定时器事件 | `void` | 文件写时间变化才触发 `Load` |
+| `Refresh_Click(sender, e)` | 点击事件 | `void` | 手动 `Load` |
+| `Clear_Click(sender, e)` | 点击事件 | `void` | `"wb"` 截断日志文件 + `Load` |
+
+## LicensePage.xaml.cpp（许可页）
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `LicensePage::LicensePage()` | 无 | 无（构造） | 设置 MIT 许可证全文到 `LicenseText` |
+| `BackButton_Click(sender, e)` | 点击事件 | `void` | `Frame.GoBack()` 返回上一页 |
+
+## App.xaml.cpp（应用入口）
+
+| 函数 | 输入 | 输出 | 说明/副作用 |
+|---|---|---|---|
+| `App::App()` | 无 | 无（构造） | Debug 模式下注册 `UnhandledException` 回调触发 `__debugbreak` |
+| `App::OnLaunched(e)` | 启动参数 | `void` | 创建 `MainWindow` 并 `Activate` 显示 |
+
+全局变量：`window`（主窗口实例引用，供各页面获取 HWND 或操作窗口属性）。
-- 
2.34.1

