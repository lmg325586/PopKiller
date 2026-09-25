#include "pch.h"
#include "PopupBlockerPage.xaml.h"
#include "AppSettings.h"
#include "PopupBlocker.h"
#include "RuleStorage.h" 
#include "WindowPicker.h"
#include "App.xaml.h"
#include "RuleIOPage.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Microsoft.System.h>   // winrt::Microsoft::System::DispatcherQueue
#include <algorithm>
#include <sstream>
#include <mutex>
#include <utility>
#if __has_include("PopupBlockerPage.g.cpp")
#include "PopupBlockerPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    const wchar_t* ListTypeKey(int idx) { return idx == 1 ? L"W" : L"B"; }
    const wchar_t* ListTypeLabel(int idx) { return idx == 1 ? L"白名单" : L"黑名单"; }

    const wchar_t* FieldKey(int idx)
    {
        switch (idx) {
        case 0:  return L"exe";
        case 1:  return L"path";
        case 2:  return L"title";
        default: return L"class";
        }
    }

    const wchar_t* FieldLabel(int idx)
    {
        switch (idx) {
        case 0:  return L"进程";
        case 1:  return L"路径";
        case 2:  return L"标题";
        default: return L"类名";
        }
    }

    const wchar_t* MatchModeKey(int idx)
    {
        switch (idx) {
        case 0:  return L"contains";
        case 1:  return L"exact";
        default: return L"wildcard";
        }
    }

    const wchar_t* MatchModeLabel(int idx)
    {
        switch (idx) {
        case 0:  return L"包含";
        case 1:  return L"精确";
        default: return L"通配符";
        }
    }
}

namespace winrt::winui::implementation
{
    // 页面自身回调的 owner 令牌：即 PopupBlockerPage 实现对象地址。
    // winrt 中 get_self<impl>(abi_arg)<interface> 只是对同一指数的静态转换，
    // 因此该值与注册时写入槽位的 owner、以及 ClearCallbackIfOwnedBy 的比对值一致。
    namespace
    {
        inline const void* PageOwnerToken(PopupBlockerPage* self) noexcept
        {
            return static_cast<const void*>(self);
        }
    }

    // 页面自身回调入口对应的自由函数（非 lambda），配合 owner_bind 携带
    // owner = 本页面实现对象指针，使析构/卸载时能"只清除自己注册的回调"。
    // 注意：这些入口可能由托盘线程或引擎协程续体线程经 SafeInvoke 调用（不持锁执行），
    // 一切 UI 操作必须调度回 UI 线程，并防止页面在排队期间被销毁。
    //
    // ============================ 编译错误修复说明 ============================
    // [C2440] "初始化": 无法从 "initializer list" 转换为 "winrt::weak_ref<D>"
    //   原写法 winrt::weak_ref<PopupBlockerPage> weak{ *self }; 非法：
    //   C++/WinRT 的 weak_ref<T> 仅接受派生自 winrt::weak_ref_source 的类型
    //   （投影类 / COM 引用 / get_weak() 返回值）。实现类 PopupBlockerPageT<>
    //   并不派生自 weak_ref_source，用 *self 做列表初始化会命中不可访问的基类
    //   构造（winrt_weak_ref 的 protected 构造走 initializer-list 路径），故报
    //   "无法从 initializer list 转换"。成员函数内的正确写法是 CTAD + get_weak：
    //       winrt::weak_ref weak = self.get_weak();
    //   但这两个入口只有裸 ctx 指针可用——页面随时可能在其他线程销毁，解引用
    //   ctx 提升弱引用本身就是 use-after-free，因此这里彻底不使用 weak_ref，
    //   改用"存活校验 + 强引用投影对象入队"方案（见下）。
    // [C2248] 无法访问 private 成员 UpdateCommunityStatus
    //   友元声明不跨命名空间查找：回调入口位于 implementation 命名空间内的
    //   匿名命名空间，而 UpdateCommunityStatus 是 private 成员，直接调用报
    //   C2248。修复方式：不再从自由函数触碰私有成员，统一改走公开的
    //   OnExternalStateChanged()（public；内部完成开关同步、状态刷新与
    //   ReloadRulesFromEngine 社区规则重同步，覆盖两类外部变化；其内部的
    //   get_strong() 兜底保证对象有效）。
    // [E1696 / 第二处 C2440] IntelliSense 级联误报
    //   上述真实编译错误会使 MSBuild 跳过 XAML 代码生成步骤（App.xaml.g.hpp /
    //   *.g.h 未产出），IntelliSense 随之报 E1696 无法打开源文件 "App.xaml.g.hpp"，
    //   并对同一 weak_ref 初始化再报一次 C2440。修掉根因后此组错误自动消失；
    //   若 IDE 仍显示旧错误，清理 obj/bin 后重新生成即可再生成 .g.hpp。
    // ==========================================================================
    namespace
    {
        // 把"通知存活页面刷新"的队列项投递到 UI 线程。
        // page 为当前存活的 PopupBlockerPage 投影对象（强引用），lambda 捕获它即可
        // 安全跨越排队窗口期：页面即便随后被导航销毁，强引用也会延长其生命周期，
        // 队列项执行时对象必然有效。
        void EnqueuePageRefresh(winrt::winui::PopupBlockerPage const& page)
        {
            // 从 XAML 线程上下文获取所属 DispatcherQueue（WinUI 3 队列类型为
            // Microsoft::System::DispatcherQueue；XamlTypeInfo 程序集中并无
            // XamlDispatcherQueueProvider，原写法导致 C2039/C2065）。
            auto queue = Microsoft::System::DispatcherQueue::GetForCurrentThread();
            if (!queue) return;   // 不在 STA/XAML 消息循环环境（如进程退出阶段）：放弃刷新
            queue.TryEnqueue([page]()
                {
                    winrt::get_self<PopupBlockerPage>(page)->OnExternalStateChanged();
                });
        }

        // 当前存活的 PopupBlockerPage 页面登记表（弱引用列表）。
        // 页面在 Loaded 时登记、Unloaded/析构时注销；登记与注销都发生在 UI 线程。
        // 自由回调入口据此把"裸 owner 指针"换算成公开投影对象（强引用）：
        //   - 查无此页 -> 页面正在/已经销毁，立即返回，全程不解引用 ctx；
        //   - 查到     -> lock() 提升弱引用成功，拿到必然有效的强引用。
        // 相比原方案里对已可能销毁的对象构造 winrt::weak_ref（C2440）或调用私有
        // 成员 UpdateCommunityStatus（C2248），该方案既编译合法又线程安全。
        std::mutex g_pageRegistryMutex;
        std::vector<winrt::weak_ref<winrt::winui::PopupBlockerPage>> g_livePages;

        void RegisterLivePage(winrt::winui::PopupBlockerPage const& page)
        {
            std::lock_guard lock(g_pageRegistryMutex);
            g_livePages.push_back(winrt::weak_ref{ page });
        }

        // 注销页面登记（按 owner 令牌比对，同时顺带清理已失效的弱引用项）。
        // 仅在 UI 线程调用（Unloaded / 析构），提升比对无并发销毁风险。
        void UnregisterLivePage(const void* owner)
        {
            std::lock_guard lock(g_pageRegistryMutex);
            for (auto it = g_livePages.begin(); it != g_livePages.end(); )
            {
                // winrt::weak_ref<T>::lock() 直接返回强引用投影对象 T（而非智能指针），
                // 因此不能对返回值再做解引用（原 *it->lock() 导致 E0063/C2679）。
                auto page = it->lock();
                if (!page ||
                    static_cast<const void*>(winrt::get_self<PopupBlockerPage>(page)) == owner)
                    it = g_livePages.erase(it);   // 死项或命中项：移除
                else
                    ++it;
            }
        }

        // 返回与 owner 令牌匹配的存活页面投影对象（强引用）；查无此页返回 nullptr。
        // weak_ref::lock() 本身线程安全（内部原子地检查源对象并提升），因此可在
        // 后台回调线程直接对表项尝试提升：成功即证明对象此刻必然有效；失败说明
        // 页面正在/已经销毁，跳过即可——全程不解引用可能已悬空的 ctx。
        winrt::winui::PopupBlockerPage LivePageFromToken(const void* owner)
        {
            std::vector<winrt::weak_ref<winrt::winui::PopupBlockerPage>> snapshot;
            {
                std::lock_guard lock(g_pageRegistryMutex);
                snapshot = g_livePages;   // 拷贝 weak_ref 列表（不触碰目标对象）
            }
            for (auto const& w : snapshot)
            {
                // 同上：w.lock() 返回 winrt::winui::PopupBlockerPage 投影对象，直接传入
                // get_self 即可，无需（也不能）解引用；get_self 结果与 RegisterLivePage
                // 时写入的 owner 令牌同为实现对象指针，类型匹配。
                if (auto page = w.lock())
                {
                    if (static_cast<const void*>(winrt::get_self<PopupBlockerPage>(page)) == owner)
                        return page;      // 提升成功：引用计数 >= 2，对象必然存活
                }
            }
            return nullptr;
        }
    }

    void PageEnabledChanged(void* ctx)
    {
        // 双重存活校验：
        //   a. IsCallbackOwnedBy —— 持 CallbackMutex，与页面析构/Unloaded 的
        //      ClearCallbackIfOwnedBy 摘除路径互斥；槽位已被摘除/替换则直接返回。
        //   b. LivePageFromToken —— 从页面登记表提升弱引用，拿到必然有效的强引用
        //      投影对象；查无此页同样立即返回，全程不解引用可能已销毁的 ctx。
        if (!PopupBlocker::IsCallbackOwnedBy(PopupBlocker::EnabledChangedCallback, ctx))
            return;
        if (auto page = LivePageFromToken(ctx)) EnqueuePageRefresh(page);
    }

    void PageCommunityRulesFetched(void* ctx, bool ok, std::wstring msg)
    {
        // 引擎协程续体线程 -> UI 线程；同上，先校验存活、再取投影对象入队。
        // 拉取结果（ok/msg）无需在此消费：OnExternalStateChanged ->
        // ReloadRulesFromEngine 会从引擎重新同步社区规则与列表显示。
        if (!PopupBlocker::IsCallbackOwnedBy(PopupBlocker::CommunityRulesFetchCallback, ctx))
            return;
        (void)ok; (void)msg;
        if (auto page = LivePageFromToken(ctx)) EnqueuePageRefresh(page);
    }

    PopupBlockerPage::~PopupBlockerPage()
    {
        if (m_statusTimer) m_statusTimer.Stop();
        // 注销页面登记：此后任何后台线程的回调入口经 LivePageFromToken 都查不到
        // 本页面，不会再触碰正在销毁的对象（弱引用也已自动失效，双保险）。
        UnregisterLivePage(PageOwnerToken(this));
        // 只清除"自己注册的"回调（按 owner 指针比对，内部持有 CallbackMutex），
        // 不误伤 MainWindow/App 层常驻回调；引擎线程持锁读取，无数据竞争。
        PopupBlocker::ClearCallbackIfOwnedBy(
            PopupBlocker::EnabledChangedCallback, PageOwnerToken(this));
        PopupBlocker::ClearCallbackIfOwnedBy(
            PopupBlocker::CommunityRulesFetchCallback, PageOwnerToken(this));
    }

    PopupBlockerPage::PopupBlockerPage()
    {
        InitializeComponent();

        this->NavigationCacheMode(Navigation::NavigationCacheMode::Disabled);

        m_statusTimer = DispatcherTimer();
        m_statusTimer.Interval(std::chrono::milliseconds{ 500 });
        m_statusTimer.Tick({ get_weak(), &PopupBlockerPage::StatusTimer_Tick });
        m_statusTimer.Start();
        m_resumeButton = winrt::Microsoft::UI::Xaml::Controls::Button();
        m_resumeButton.Content(winrt::box_value(L"立即恢复"));
        m_resumeButton.Click({ get_weak(), &PopupBlockerPage::ResumeButton_Click });

        PopupBlocker::EnsureDefaultRules();

        EnableToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1);

        // 页面自身回调令牌：与 MainWindow 常驻注册（owner = MainWindow*）区分，
        // 使析构/卸载时能通过 ClearCallbackIfOwnedBy 只摘除自己注册的那份。
        m_callbackOwnerToken = PageOwnerToken(this);

        this->Loaded([this, self = get_strong()](auto&&, auto&&)
            {
                // 登记存活页面（投影对象弱引用），供自由回调入口安全换算强引用。
                // NavigationCacheMode::Disabled 下 Loaded 每实例至多触发一次，无需去重。
                RegisterLivePage(self);

                // 仅当槽位为空（MainWindow 尚未注册，或其常驻回调已被退出清理摘除）时，
                // 才临时注册页面回调用于外部状态变化时刷新本页面 UI；
                // 绝不覆盖 MainWindow 层常驻回调——托盘切换开关 / 社区规则后台更新
                // 必须始终先经 MainWindow 同步"设置 -> 引擎"，用户离开本页后依然生效。
                std::lock_guard lock(PopupBlocker::CallbackMutex);
                if (!PopupBlocker::EnabledChangedCallback)
                {
                    owner_bind(PopupBlocker::EnabledChangedCallback,
                        &PageEnabledChanged, m_callbackOwnerToken);
                }
                if (!PopupBlocker::CommunityRulesFetchCallback)
                {
                    owner_bind(PopupBlocker::CommunityRulesFetchCallback,
                        &PageCommunityRulesFetched, m_callbackOwnerToken);
                }
            });

        this->Unloaded([this](auto&&, auto&&)
            {
                if (m_statusTimer) m_statusTimer.Stop();
                // 离开页面即注销登记，回调入口不再向本页派发刷新。
                UnregisterLivePage(m_callbackOwnerToken);
                // 离开页面时停止向已不可见的 UI 派发回调；只清除自己注册的回调，
                // 绝不清空 MainWindow/App 层常驻回调（托盘/后台线程依赖它们同步引擎状态）。
                PopupBlocker::ClearCallbackIfOwnedBy(
                    PopupBlocker::EnabledChangedCallback, m_callbackOwnerToken);
                PopupBlocker::ClearCallbackIfOwnedBy(
                    PopupBlocker::CommunityRulesFetchCallback, m_callbackOwnerToken);
            });

        m_initialized = true;

        PopupBlocker::SyncFromSettings();
        ReloadRulesFromEngine();

        CommunityRulesToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"CommunityRulesEnabled", 1) == 1);
        if (CommunityRulesToggle().IsOn()) {
            CommunityStatusText().Text(L"正在拉取社区规则…");
            PopupBlocker::FetchCommunityRulesAsync();
        }
    }

    PopupBlocker::Rule PopupBlockerPage::ToEngineRule(RuleItem const& it)
    {
        PopupBlocker::Rule r;
        r.isWhitelist = (it.listType == 1);
        switch (it.fieldType) {
        case 0: r.field = PopupBlocker::RuleField::Exe; break;
        case 1: r.field = PopupBlocker::RuleField::Path; break;
        case 2: r.field = PopupBlocker::RuleField::Title; break;
        default: r.field = PopupBlocker::RuleField::Class; break;
        }
        switch (it.matchMode) {
        case 0: r.mode = PopupBlocker::MatchMode::Contains; break;
        case 1: r.mode = PopupBlocker::MatchMode::Exact; break;
        default: r.mode = PopupBlocker::MatchMode::Wildcard; break;
        }
        r.pattern = PopupBlocker::Lower(it.pattern);
        r.fromCommunity = it.fromCommunity;
        return r;
    }

    void PopupBlockerPage::ReloadRulesFromEngine()
    {
        m_rules.clear();
        std::vector<PopupBlocker::Rule> cur;
        { std::lock_guard lock(PopupBlocker::RulesMutex); cur = PopupBlocker::Rules; }
        for (auto const& r : cur)
        {
            RuleItem item{};
            item.listType = r.isWhitelist ? 1 : 0;
            item.fieldType = static_cast<int>(r.field);
            item.matchMode = static_cast<int>(r.mode);
            item.pattern = r.pattern;
            item.fromCommunity = r.fromCommunity;
            m_rules.push_back(item);
        }

        std::stable_partition(m_rules.begin(), m_rules.end(),
            [](RuleItem const& r) { return !r.fromCommunity; });

        RefreshList();
        UpdateCommunityRestoreButtonVisibility();
    }

    void PopupBlockerPage::RefreshList()
    {
        RulesList().Items().Clear();
        m_visibleIndex.clear();

        auto appendItem = [this](size_t i)
            {
                auto const& r = m_rules[i];
                std::wstring display = (r.fromCommunity ? L"[社区] " : L"") +
                    std::wstring(ListTypeLabel(r.listType)) + L" | " +
                    FieldLabel(r.fieldType) + L" | " +
                    MatchModeLabel(r.matchMode) + L"：" + r.pattern;

                if (!m_searchText.empty() &&
                    PopupBlocker::Lower(display).find(m_searchText) == std::wstring::npos)
                    return;

                m_visibleIndex.push_back(i);
                RulesList().Items().Append(box_value(hstring(display)));
            };

        for (size_t i = 0; i < m_rules.size(); ++i)
            if (!m_rules[i].fromCommunity) appendItem(i);
        for (size_t i = 0; i < m_rules.size(); ++i)
            if (m_rules[i].fromCommunity) appendItem(i);

        UpdateCommunityRestoreButtonVisibility();
    }

    void PopupBlockerPage::Save()
    {
        std::vector<PopupBlocker::Rule> newRules;
        for (auto const& r : m_rules) {
            newRules.push_back(ToEngineRule(r));
        }
        PopupBlocker::SaveRules(newRules);
    }

    void PopupBlockerPage::EnableToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        auto self = get_strong();
        if (!self) return;

        // 由 OnExternalStateChanged / OnNavigatedTo 程序性同步开关时抑制回写，
        // 避免"外部状态 -> UI -> 再写设置/引擎"的反向干扰。
        if (!m_initialized) return;

        bool on = EnableToggle().IsOn();
        AppSettings::WriteInt(L"Blocker", L"Enabled", on ? 1 : 0);
        if (on)
        {
            PopupBlocker::SyncFromSettings();
            HeuristicML::GetInstance().Init();
            PopupBlocker::Start();
            RefreshStatus();
        }
        else
        {
            PopupBlocker::Stop();
            RefreshStatus();
        }
    }

    void PopupBlockerPage::CommunityRulesToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        auto self = get_strong();
        if (!self) return;

        if (!m_initialized) return;
        bool on = CommunityRulesToggle().IsOn();
        AppSettings::WriteInt(L"Blocker", L"CommunityRulesEnabled", on ? 1 : 0);

        if (on) {
            CommunityStatusText().Text(L"正在拉取社区规则…");
            RetryFetchButton().Visibility(Visibility::Collapsed);
            PopupBlocker::FetchCommunityRulesAsync();
        }
        else {
            CommunityStatusText().Text(L"");
            RetryFetchButton().Visibility(Visibility::Collapsed);
        }
    }

    void PopupBlockerPage::UpdateCommunityStatus(bool ok, std::wstring const& msg)
    {
        auto self = get_strong();
        if (!self) return;

        if (!CommunityRulesToggle().IsOn()) {
            CommunityStatusText().Text(L"");
            RetryFetchButton().Visibility(Visibility::Collapsed);
            return;
        }
        if (ok) {
            CommunityStatusText().Text(L"社区规则已更新，新增 " + winrt::hstring(msg) + L" 条");
            CommunityStatusText().Foreground(Media::SolidColorBrush(
                winrt::Windows::UI::Color{ 0xFF, 0x80, 0x80, 0x80 }));
            RetryFetchButton().Visibility(Visibility::Collapsed);
            ReloadRulesFromEngine();
        }
        else {
            CommunityStatusText().Text(L"社区规则拉取失败：" + winrt::hstring(msg));
            CommunityStatusText().Foreground(Media::SolidColorBrush(
                winrt::Windows::UI::Color{ 0xFF, 0xE6, 0xA2, 0x3C }));
            RetryFetchButton().Visibility(Visibility::Visible);
        }
    }

    void PopupBlockerPage::RetryFetchButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto self = get_strong();
        if (!self) return;

        CommunityStatusText().Text(L"正在拉取社区规则…");
        RetryFetchButton().Visibility(Visibility::Collapsed);
        PopupBlocker::FetchCommunityRulesAsync();
    }

    void PopupBlockerPage::EditRule_Click(IInspectable const&, RoutedEventArgs const&)
    {
        size_t real = (size_t)-1;
        if (m_rightClickRealIndex != (size_t)-1 && m_rightClickRealIndex < m_rules.size())
        {
            real = m_rightClickRealIndex;
            m_rightClickRealIndex = (size_t)-1;
        }
        else
        {
            int idx = RulesList().SelectedIndex();
            if (idx < 0 || idx >= static_cast<int>(m_visibleIndex.size())) return;
            real = m_visibleIndex[static_cast<size_t>(idx)];
        }

        OpenEditDialog(real);
    }

    void PopupBlockerPage::AddRule_Click(IInspectable const&, RoutedEventArgs const&)
    {
        hstring text = PatternInput().Text();
        if (text.empty()) return;

        int listType = ListTypeCombo().SelectedIndex();
        int fieldType = RuleTypeCombo().SelectedIndex();
        int matchMode = MatchModeCombo().SelectedIndex();
        std::wstring pattern{ text };
        std::wstring patternLower = PopupBlocker::Lower(pattern);

        bool conflict = false;
        for (size_t i = 0; i < m_rules.size(); ++i)
        {
            auto const& r = m_rules[i];
            if (r.listType != listType &&
                r.fieldType == fieldType &&
                r.matchMode == matchMode &&
                PopupBlocker::Lower(r.pattern) == patternLower)
            {
                conflict = true;
                break;
            }
        }

        m_rules.insert(m_rules.begin(), { listType, fieldType, matchMode, pattern, false });

        PatternInput().Text(L"");
        Save();
        RefreshList();

        if (conflict)
        {
            PickInfo().Text(L"⚠ 注意：已存在相同内容的相反名单规则；白名单优先，该窗口将被放行。");
            PickInfo().Foreground(Media::SolidColorBrush(
                winrt::Windows::UI::Color{ 0xFF, 0xE6, 0xA2, 0x3C }));
        }
        else
        {
            PickInfo().Text(L"");
            PickInfo().Foreground(Media::SolidColorBrush(
                winrt::Windows::UI::Color{ 0xFF, 0x80, 0x80, 0x80 }));
        }
    }

    void PopupBlockerPage::DeleteRule_Click(IInspectable const&, RoutedEventArgs const&)
    {
        size_t real = (size_t)-1;
        if (m_rightClickRealIndex != (size_t)-1 && m_rightClickRealIndex < m_rules.size())
        {
            real = m_rightClickRealIndex;
            m_rightClickRealIndex = (size_t)-1;
        }
        else
        {
            int idx = RulesList().SelectedIndex();
            if (idx < 0 || idx >= static_cast<int>(m_visibleIndex.size())) return;
            real = m_visibleIndex[static_cast<size_t>(idx)];
        }

        if (m_rules[real].fromCommunity) {
            std::lock_guard lock(PopupBlocker::RulesMutex);
            PopupBlocker::CommunityRemoved.push_back(PopupBlocker::RuleKey(ToEngineRule(m_rules[real])));
        }

        m_rules.erase(m_rules.begin() + real);
        Save();
        RefreshList();
    }

    void PopupBlockerPage::Pick_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto window = winrt::winui::implementation::App::window;
        if (!window) return;
        auto native = window.try_as<::IWindowNative>();
        HWND hwnd{};
        if (!native || FAILED(native->get_WindowHandle(&hwnd)) || !hwnd) return;

        WindowPicker::Start(hwnd, [this](WindowPicker::PickResult r)
            {
                int fieldIdx = RuleTypeCombo().SelectedIndex();
                std::wstring value;
                switch (fieldIdx) {
                case 0:  value = r.exe;         break;
                case 1:  value = r.processPath; break;
                case 2:  value = r.title;       break;
                default: value = r.className;   break;
                }
                if (value.empty()) value = r.exe;

                PatternInput().Text(hstring(value));
                PickInfo().Text(L"exe: " + r.exe +
                    L"\npath: " + r.processPath +
                    L"\nclass: " + r.className +
                    L"\ntitle: " + r.title);
                PickInfo().Foreground(Media::SolidColorBrush(
                    winrt::Windows::UI::Color{ 0xFF, 0x80, 0x80, 0x80 }));
            });
    }

    void PopupBlockerPage::SearchInput_TextChanged(IInspectable const&,
        Controls::TextChangedEventArgs const&)
    {
        m_searchText = PopupBlocker::Lower(std::wstring(SearchInput().Text()));
        RefreshList();
    }

    void PopupBlockerPage::OpenIO_Click(IInspectable const&, RoutedEventArgs const&)
    {
        Frame().Navigate(xaml_typename<winui::RuleIOPage>());
    }

    void PopupBlockerPage::StatusTimer_Tick(IInspectable const&, IInspectable const&)
    {
        if (PopupBlocker::ShuttingDown.load()) return;

        auto self = get_strong();
        if (!self) return;
        RefreshStatus();
    }

    void PopupBlockerPage::RefreshStatus()
    {
        auto self = get_strong();
        if (!self) return;

        bool enabled = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
        bool paused = PopupBlocker::Paused.load();

        using namespace winrt::Microsoft::UI::Xaml::Controls;

        if (!enabled) {
            StatusInfoBar().Severity(InfoBarSeverity::Informational);
            StatusInfoBar().Title(L"拦截已关闭");
            StatusInfoBar().Message(L"当前不会拦截任何弹窗。");
            StatusInfoBar().ActionButton(nullptr);
        }
        else if (paused) {
            StatusInfoBar().Severity(InfoBarSeverity::Warning);
            StatusInfoBar().Title(L"拦截已暂停");

            long long deadline = PopupBlocker::PauseDeadlineMs.load();
            long long now = PopupBlocker::NowMs();
            long long remaining = deadline - now;
            if (remaining < 0) remaining = 0;

            int totalSeconds = static_cast<int>(remaining / 1000);
            int minutes = totalSeconds / 60;
            int seconds = totalSeconds % 60;

            std::wstring msg = L"将在 " + std::to_wstring(minutes) + L" 分 "
                + std::to_wstring(seconds) + L" 秒后自动恢复拦截。";
            StatusInfoBar().Message(winrt::hstring(msg));
            StatusInfoBar().ActionButton(m_resumeButton);
        }
        else {
            StatusInfoBar().Severity(InfoBarSeverity::Success);
            StatusInfoBar().Title(L"拦截运行中");
            StatusInfoBar().Message(L"正在实时监控并拦截弹窗。");
            StatusInfoBar().ActionButton(nullptr);
        }
    }

    void PopupBlockerPage::ResumeButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        PopupBlocker::ResumeNow();
        RefreshStatus();
    }

    void PopupBlockerPage::OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&)
    {
        // 重新进入页面：从设置/引擎全量重新同步 UI（开关、状态条、规则列表），
        // 确保与托盘/后台线程在页面离开期间所做的变更保持一致，不出现状态漂移。
        m_initialized = false;
        EnableToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1);
        CommunityRulesToggle().IsOn(
            AppSettings::ReadInt(L"Blocker", L"CommunityRulesEnabled", 1) == 1);
        m_initialized = true;

        if (!m_statusTimer) m_statusTimer.Start();
        RefreshStatus();
        ReloadRulesFromEngine();
        RefreshList();
    }

    void PopupBlockerPage::OnNavigatedFrom(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&)
    {
        if (m_statusTimer) m_statusTimer.Stop();
        // 只清除"自己注册的"回调（按 owner 令牌比对，内部持有 CallbackMutex）。
        // 绝不能无条件置空：MainWindow/App 层常驻回调必须存活，
        // 否则用户离开本页后，托盘切换开关 / 社区规则后台更新的回调将丢失，
        // 引擎状态与设置脱节，重新进入页面时 UI 与实际拦截状态不一致。
        PopupBlocker::ClearCallbackIfOwnedBy(
            PopupBlocker::EnabledChangedCallback, m_callbackOwnerToken);
        PopupBlocker::ClearCallbackIfOwnedBy(
            PopupBlocker::CommunityRulesFetchCallback, m_callbackOwnerToken);
    }

    // UI 线程：由 MainWindow 常驻回调（托盘切换开关）或本页面自身回调触发，
    // 从设置与引擎重新同步本页面 UI，消除"UI 状态与实际拦截状态不一致"。
    void PopupBlockerPage::OnExternalStateChanged()
    {
        auto self = get_strong();
        if (!self) return;

        // 外部（托盘/后台）改动了开关：先同步开关显示（抑制 Toggled 回写，避免反向干扰引擎），
        // 再刷新运行状态与规则列表。
        const bool on = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
        if (EnableToggle().IsOn() != on)
        {
            m_initialized = false;
            EnableToggle().IsOn(on);
            m_initialized = true;
        }

        RefreshStatus();
        ReloadRulesFromEngine();
    }

    void PopupBlockerPage::RuleItem_RightTapped(winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const&)
    {
        m_rightClickRealIndex = (size_t)-1;
        if (auto tb = sender.try_as<Controls::TextBlock>())
        {
            uint32_t uiIdx = 0;
            if (RulesList().Items().IndexOf(box_value(tb.Text()), uiIdx)
                && uiIdx < m_visibleIndex.size())
            {
                m_rightClickRealIndex = m_visibleIndex[uiIdx];
                RulesList().SelectedIndex(uiIdx);
            }
        }
    }

    void PopupBlockerPage::RuleItem_DoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&)
    {
        if (auto tb = sender.try_as<Controls::TextBlock>())
        {
            uint32_t uiIdx = 0;
            if (RulesList().Items().IndexOf(box_value(tb.Text()), uiIdx) && uiIdx < m_visibleIndex.size())
                OpenEditDialog(m_visibleIndex[uiIdx]);
        }
    }

    winrt::fire_and_forget PopupBlockerPage::OpenEditDialog(size_t real)
    {
        auto lifetime = get_strong();
        if (real >= m_rules.size()) co_return;
        auto const& it = m_rules[real];

        auto xamlRoot = this->XamlRoot();
        if (!xamlRoot) co_return;

        EditDialog().XamlRoot(xamlRoot);
        EditListTypeCombo().SelectedIndex(it.listType);
        EditFieldCombo().SelectedIndex(it.fieldType);
        EditMatchModeCombo().SelectedIndex(it.matchMode);
        EditPatternInput().Text(hstring(it.pattern));
        EditCommunityNote().Visibility(it.fromCommunity ? Visibility::Visible : Visibility::Collapsed);

        auto result = co_await EditDialog().ShowAsync();
        if (result != Controls::ContentDialogResult::Primary) co_return;

        hstring text = EditPatternInput().Text();
        if (text.empty())
        {
            PickInfo().Text(L"⚠ 模式串不能为空，未保存。");
            PickInfo().Foreground(Media::SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0xE6, 0xA2, 0x3C }));
            co_return;
        }

        int listType = EditListTypeCombo().SelectedIndex();
        int fieldType = EditFieldCombo().SelectedIndex();
        int matchMode = EditMatchModeCombo().SelectedIndex();
        std::wstring pattern{ text };
        std::wstring patternLower = PopupBlocker::Lower(pattern);

        bool conflict = false;
        for (size_t i = 0; i < m_rules.size(); ++i)
        {
            if (i == real) continue;
            auto const& r = m_rules[i];
            if (r.listType != listType && r.fieldType == fieldType &&
                r.matchMode == matchMode && PopupBlocker::Lower(r.pattern) == patternLower)
            {
                conflict = true; break;
            }
        }

        auto& old = m_rules[real];
        if (old.fromCommunity) {
            std::lock_guard lock(PopupBlocker::RulesMutex);
            PopupBlocker::CommunityRemoved.push_back(PopupBlocker::RuleKey(ToEngineRule(old)));
        }
        old = { listType, fieldType, matchMode, pattern, false };

        Save();
        RefreshList();

        if (conflict) {
            PickInfo().Text(L"⚠ 注意：已存在相同内容的相反名单规则；白名单优先，该窗口将被放行。");
            PickInfo().Foreground(Media::SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0xE6, 0xA2, 0x3C }));
        }
        else {
            PickInfo().Text(L"");
        }
    }

    void PopupBlockerPage::RestoreCommunity_Click(IInspectable const&, RoutedEventArgs const&)
    {
        bool changed = false;
        std::vector<PopupBlocker::Rule> cur;
        {
            std::lock_guard lock(PopupBlocker::RulesMutex);
            if (!PopupBlocker::CommunityRemoved.empty()) {
                PopupBlocker::CommunityRemoved.clear();
                changed = true;
                cur = PopupBlocker::Rules;
            }
        }

        if (changed) {
            PopupBlocker::SaveRules(cur);

            UpdateCommunityRestoreButtonVisibility();
            CommunityStatusText().Text(L"正在重新合并社区规则…");
            RetryFetchButton().Visibility(Visibility::Collapsed);

            PopupBlocker::FetchCommunityRulesAsync();
        }
    }

    void PopupBlockerPage::UpdateCommunityRestoreButtonVisibility()
    {
        bool hasTombs = false;
        {
            std::lock_guard lock(PopupBlocker::RulesMutex);
            hasTombs = !PopupBlocker::CommunityRemoved.empty();
        }
        if (RestoreCommunityButton()) {
            RestoreCommunityButton().Visibility(hasTombs ? Visibility::Visible : Visibility::Collapsed);
        }
    }
}