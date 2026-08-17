MainWindow.xaml.cpp
LRESULT CALLBACK MinSizeSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) — 消息+参数 → LRESULT（托盘/最小尺寸拦截）
MainWindow::MainWindow() — 无 → 无（构造）
void NavView_SelectionChanged(NavigationView const&, NavigationViewSelectionChangedEventArgs const&) — 选中事件 → 导航页面
void NavigateToTag(hstring const& tag) — Tag 字符串 → 选中对应导航项
HomePage.xaml.cpp
std::wstring ReadRegString(HKEY key, LPCWSTR value) — 注册表句柄+值名 → 字符串值
std::wstring EditionName(std::wstring const& id) — EditionID → 中文版本名
void PlaceEllipse(Shapes::Ellipse const& el, double x, double y) — 椭圆+中心坐标 → 设置 Canvas Left/Top
void ClipToSelf(FrameworkElement const& el) — 元素 → 裁剪到自身矩形
void UpdateGlow(FrameworkElement const& card, UIElement const& canvas, std::initializer_list<Shapes::Ellipse> layers, PointerRoutedEventArgs const&) — 卡片+画布+椭圆组+指针事件 → 设置透明度与椭圆位置
HomePage::HomePage() — 无 → 无
void RootPointerMoved(IInspectable const&, PointerRoutedEventArgs const&) — 指针事件 → 更新两卡辉光
void RootPointerExited(IInspectable const&, PointerRoutedEventArgs const&) — 无有效输入 → 辉光归零
void GoToBlocker_Tapped(IInspectable const&, TappedRoutedEventArgs const&) — 点击 → 跳转拦截页
SettingsPage.xaml.cpp
SettingsPage::SettingsPage() — 无 → 无
void LicenseLink_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → 导航许可页
void ThemeComboBox_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) — 选择事件 → 写配置+切材质/标题栏
void ForceBlockToggle_Toggled(IInspectable const&, RoutedEventArgs const&) — 开关事件 → 写配置+更新 PopupBlocker::ForceBlock
PopupBlockerPage.xaml.cpp
PopupBlockerPage::PopupBlockerPage() — 无 → 无
void EnableToggle_Toggled(IInspectable const&, RoutedEventArgs const&) — 开关事件 → 启/停引擎+写配置
void AddRule_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → 新增规则并持久化
void DeleteRule_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → 删除规则并持久化
void Pick_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → 启动拾取器，回调填充输入框
void RefreshList()（私有）— 无 → 渲染规则列表
void Save()（私有）— 无 → 规则写回 ini
PopupBlocker.h
std::wstring LogPath() — 无 → 日志文件全路径
std::wstring Lower(std::wstring s) — 字符串 → 小写副本
void InitSelfExe() — 无 → 设置 SelfExe
bool LooksLikePopup(HWND hwnd) — 窗口句柄 → 是否弹窗形态
void EnsureDefaultRules() — 无 → 首跑写默认规则
void SyncFromSettings() — 无 → 刷新 ForceBlock+Rules
void Start() / void Stop() — 无 → 起/停钩子线程
detail::void Log(std::wstring const& s) — 日志内容 → 追加一行到文件
detail::std::wstring GetProcessName(HWND hwnd) — 句柄 → 小写进程名
detail::bool IsProtected(HWND hwnd) — 句柄 → 是否白名单/自身
detail::std::wstring GetTitle(HWND hwnd) / GetClass(HWND hwnd) — 句柄 → 小写标题/类名
detail::int Match(HWND hwnd) — 句柄 → 命中规则下标，-1 未命中
detail::void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) — 系统事件 → 判定并关窗+记日志
detail::DWORD WINAPI ThreadMain(LPVOID) — 无 → 钩子消息循环线程体
WindowPicker.h
detail::LRESULT CALLBACK FrameProc(HWND, UINT, WPARAM, LPARAM) — 消息 → LRESULT
detail::void EnsureFrame() — 无 → 创建蓝框窗
detail::void PaintFrame() — 无 → 重画蓝边位图
detail::void MoveFrame() — 无 → 蓝框跟随 Target
detail::void EnsureOverlay() — 无 → 创建全屏拾取窗
detail::void Teardown(bool commit) — 是否提交 → 隐藏窗口，提交则回调结果
detail::LRESULT CALLBACK OverlayProc(HWND, UINT, WPARAM, LPARAM) — 消息 → 命中/选取/取消
void Start(HWND mainHwnd, std::function<void(PickResult)> cb) — 主窗口+回调 → 进入拾取
void Cancel() — 无 → 退出拾取
TrayIcon.h
HICON GetIcon() — 无 → exe 图标句柄
void Add() / void Remove() — 无 → 增/删托盘图标
void HideToTray() — 无 → 隐藏窗口+加图标+回调+修剪内存
void Restore() — 无 → 显示窗口+删图标+回调
void ToggleBlocker() — 无 → 切换拦截启停+写配置+触发回调
bool Handle(UINT msg, WPARAM wp, LPARAM lp) — 消息 → 是否已消费
void Init(HWND hwnd) — 主窗口句柄 → 绑定托盘宿主
AppSettings.h
std::wstring IniPath() — 无 → exe 同目录 winui.ini 全路径
void Flush() — 无 → 刷写 ini 到磁盘
int ReadInt(const wchar_t* section, const wchar_t* key, int def) — 节/键/默认值 → int
void WriteInt(const wchar_t* section, const wchar_t* key, int value) — 节/键/值 → 写 ini 并 Flush
std::wstring ReadString(const wchar_t* section, const wchar_t* key, const std::wstring& def = L"") — 节/键/默认串(可选) → 字符串
void WriteString(const wchar_t* section, const wchar_t* key, const std::wstring& value) — 节/键/值 → 写 ini 并 Flush
void DeleteKey(const wchar_t* section, const wchar_t* key) — 节/键 → 删除该键
AppTheme.h
inline int32_t Index{ 0 } — 材质索引变量（0 普通 / 1 Mica）
inline winrt::Microsoft::UI::Xaml::Controls::Panel TitleBarElement{ nullptr } — 自定义标题栏元素变量
void ApplyTitleBar(winrt::Microsoft::UI::Windowing::AppTitleBar const& titleBar) — 输入：标题栏对象 → 无返回值；按 Index 设背景（Mica=透明 / 否则白）、前景与按钮各态颜色（黑/灰/LightGray），并同步 TitleBarElement 背景
BlockLogPage.xaml.cpp
匿名命名空间：
std::wstring ReadLogText() — 无 → 读 blocklog.txt 全文，去 BOM，UTF-8 转宽串
uint64_t LogWriteTime() — 无 → 日志文件最后写时间（FILETIME 合并为 uint64），不存在返回 0
成员：
BlockLogPage::BlockLogPage() — 无 → 无；首载 + 建 1s DispatcherTimer 挂 Timer_Tick + Unloaded 停表
void Load() — 无 → 清空 LogList，按行切分倒序追加，记录 m_lastWrite
void Timer_Tick(IInspectable const&, IInspectable const&) — 无有效输入 → 写时间变化才 Load()
void Refresh_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → 手动 Load()
void Clear_Click(IInspectable const&, RoutedEventArgs const&) — 点击 → "wb" 截断日志文件 + Load()
App.xaml.cpp
App::App() — 无 → 无；在 Debug 模式下注册 UnhandledException 回调触发断点
void OnLaunched(LaunchActivatedEventArgs const& e) — 启动参数 → 实例化 MainWindow 并调用 Activate() 显示窗口
全局变量（配合 App.xaml.h）：
inline Microsoft::UI::Xaml::Window window{ nullptr }; — 主窗口实例引用，供各页面（如 HomePage、SettingsPage）获取 HWND 或操作窗口属性