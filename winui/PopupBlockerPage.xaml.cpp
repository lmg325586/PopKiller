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

    PopupBlockerPage::~PopupBlockerPage()
    {
        if (m_statusTimer) m_statusTimer.Stop();
    }

    PopupBlockerPage::PopupBlockerPage()
    {
        InitializeComponent();

        m_statusTimer = DispatcherTimer();
        m_statusTimer.Interval(std::chrono::milliseconds{ 500 });
        m_statusTimer.Tick({ this, &PopupBlockerPage::StatusTimer_Tick });
        m_statusTimer.Start();

        PopupBlocker::EnsureDefaultRules();

        EnableToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1);

        PopupBlocker::EnabledChangedCallback = [this]()
            {
                m_initialized = false;
                EnableToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1);
                m_initialized = true;
            };

        PopupBlocker::CommunityRulesFetchCallback = [this](bool ok, std::wstring msg)
            {
                DispatcherQueue().TryEnqueue([this, ok, msg]()
                    {
                        UpdateCommunityStatus(ok, msg);
                    });
            };

        this->Unloaded([this](auto&&, auto&&)
            {
                PopupBlocker::EnabledChangedCallback = nullptr;
                PopupBlocker::CommunityRulesFetchCallback = nullptr;
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
        bool on = EnableToggle().IsOn();
        AppSettings::WriteInt(L"Blocker", L"Enabled", on ? 1 : 0);
        if (on)
        {
            PopupBlocker::SyncFromSettings();
            HeuristicML::GetInstance().Init();
            PopupBlocker::Start();
        }
        else
        {
            PopupBlocker::Stop();
        }
    }

    void PopupBlockerPage::CommunityRulesToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
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
        CommunityStatusText().Text(L"正在拉取社区规则…");
        RetryFetchButton().Visibility(Visibility::Collapsed);
        PopupBlocker::FetchCommunityRulesAsync();
    }

    void PopupBlockerPage::EditRule_Click(IInspectable const&, RoutedEventArgs const&)
    {
        int idx = RulesList().SelectedIndex();
        if (idx < 0 || idx >= static_cast<int>(m_visibleIndex.size())) return;
        size_t real = m_visibleIndex[static_cast<size_t>(idx)];
        auto const& it = m_rules[real];

        ListTypeCombo().SelectedIndex(it.listType);
        RuleTypeCombo().SelectedIndex(it.fieldType);
        MatchModeCombo().SelectedIndex(it.matchMode);
        PatternInput().Text(hstring(it.pattern));
        m_editingIndex = static_cast<int>(real);
        AddRuleButton().Content(box_value(hstring(L"保存修改")));
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
            if (static_cast<int>(i) == m_editingIndex) continue;
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

        if (m_editingIndex >= 0 && m_editingIndex < static_cast<int>(m_rules.size()))
        {
            auto& old = m_rules[static_cast<size_t>(m_editingIndex)];
            if (old.fromCommunity) {
                PopupBlocker::CommunityRemoved.push_back(PopupBlocker::RuleKey(ToEngineRule(old)));
            }
            old = { listType, fieldType, matchMode, pattern, false };
            m_editingIndex = -1;
            AddRuleButton().Content(box_value(hstring(L"添加")));
        }
        else
        {
            m_rules.insert(m_rules.begin(), { listType, fieldType, matchMode, pattern, false });
        }

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
        int idx = RulesList().SelectedIndex();
        if (idx < 0 || idx >= static_cast<int>(m_visibleIndex.size())) return;

        size_t real = m_visibleIndex[static_cast<size_t>(idx)];
        if (m_rules[real].fromCommunity) {
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
        bool enabled = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
        bool paused = PopupBlocker::Paused.load();

        using namespace winrt::Microsoft::UI::Xaml::Controls;

        if (!enabled) {
            StatusInfoBar().Severity(InfoBarSeverity::Informational);
            StatusInfoBar().Title(L"拦截已关闭");
            StatusInfoBar().Message(L"当前不会拦截任何弹窗。");
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

            std::wstring msg = L"将在 " + std::to_wstring(minutes) + L" 分 " + std::to_wstring(seconds) + L" 秒后自动恢复拦截。";
            StatusInfoBar().Message(winrt::hstring(msg));
        }
        else {
            StatusInfoBar().Severity(InfoBarSeverity::Success);
            StatusInfoBar().Title(L"拦截运行中");
            StatusInfoBar().Message(L"正在实时监控并拦截弹窗。");
        }
    }
}