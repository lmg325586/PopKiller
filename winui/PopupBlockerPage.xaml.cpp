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
#include <algorithm>
#include <sstream>
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
    // 页面自身回调入口对应的自由函数（非 lambda），配合 owner_bind 携带
    // owner = 本页面 IInspectable 指针，使析构时能"只清除自己注册的回调"。
    void PageEnabledChanged(void* ctx)
    {
        auto* self = static_cast<PopupBlockerPage*>(ctx);
        if (!self) return;
        self->m_initialized = false;
        self->EnableToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1);
        self->m_initialized = true;
    }

    void PageCommunityRulesFetched(void* ctx, bool ok, std::wstring msg)
    {
        auto* self = static_cast<PopupBlockerPage*>(ctx);
        if (!self) return;
        // 引擎协程续体线程 -> UI 线程；用弱引用防止页面在排队期间被销毁。
        winrt::weak_ref<PopupBlockerPage> weak{ *self };
        self->DispatcherQueue().TryEnqueue([weak, ok, msg]()
            {
                if (auto s = weak.get()) s->UpdateCommunityStatus(ok, msg);
            });
    }

    PopupBlockerPage::~PopupBlockerPage()
    {
        if (m_statusTimer) m_statusTimer.Stop();
        // 只清除"自己注册的"回调（按 owner 指针比对，内部持有 CallbackMutex），
        // 不误伤 MainWindow/App 层常驻回调；引擎线程持锁读取，无数据竞争。
        const winrt::IInspectable token = this->try_as<IInspectable>();
        const void* selfToken = token ? token.abi() : nullptr;
        PopupBlocker::ClearCallbackIfOwnedBy(PopupBlocker::EnabledChangedCallback, selfToken);
        PopupBlocker::ClearCallbackIfOwnedBy(PopupBlocker::CommunityRulesFetchCallback, selfToken);
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
        const winrt::IInspectable token = this->try_as<IInspectable>();
        m_callbackOwnerToken = token ? token.abi() : nullptr;

        this->Loaded([this](auto&&, auto&&)
            {
                // 仅当槽位当前没有常驻注册方（MainWindow 层）时才临时注册页面回调；
                // 若被覆盖（如 MainWindow 晚于本事件重建回调），在 OnNavigatedTo 中重新注册。
                std::lock_guard lock(PopupBlocker::CallbackMutex);
                if (!PopupBlocker::CallbackOwnerOf(PopupBlocker::EnabledChangedCallback))
                {
                    owner_bind(PopupBlocker::EnabledChangedCallback,
                        &PageEnabledChanged, m_callbackOwnerToken);
                }
                if (!PopupBlocker::CallbackOwnerOf(PopupBlocker::CommunityRulesFetchCallback))
                {
                    owner_bind(PopupBlocker::CommunityRulesFetchCallback,
                        &PageCommunityRulesFetched, m_callbackOwnerToken);
                }
            });

        this->Unloaded([this](auto&&, auto&&)
            {
                if (m_statusTimer) m_statusTimer.Stop();
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
        ReloadRulesFromEngine();
        RefreshList();
    }

    void PopupBlockerPage::OnNavigatedFrom(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&)
    {
        if (m_statusTimer) m_statusTimer.Stop();
        std::lock_guard lock(PopupBlocker::CallbackMutex);
        PopupBlocker::EnabledChangedCallback = nullptr;
        PopupBlocker::CommunityRulesFetchCallback = nullptr;
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