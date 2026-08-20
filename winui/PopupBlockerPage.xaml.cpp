#include "pch.h"
#include "PopupBlockerPage.xaml.h"
#include "AppSettings.h"
#include "PopupBlocker.h"
#include "RuleStorage.h" 
#include "WindowPicker.h"
#include "App.xaml.h"
#include <microsoft.ui.xaml.window.h>
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
    PopupBlockerPage::PopupBlockerPage()
    {
        InitializeComponent();

        PopupBlocker::EnsureDefaultRules();

        EnableToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1);

        PopupBlocker::EnabledChangedCallback = [this]()
            {
                m_initialized = false;
                EnableToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1);
                m_initialized = true;
            };
        this->Unloaded([this](auto&&, auto&&)
            {
                PopupBlocker::EnabledChangedCallback = nullptr;
            });

        m_initialized = true;

        // 从 JSON 加载本地规则
        std::vector<PopupBlocker::Rule> loadedRules;
        PopupBlocker::LoadRulesJson(loadedRules);

        for (auto const& r : loadedRules)
        {
            RuleItem item{ 0, 0, 0, r.pattern };
            item.listType = r.isWhitelist ? 1 : 0;

            switch (r.field) {
            case PopupBlocker::RuleField::Exe:   item.fieldType = 0; break;
            case PopupBlocker::RuleField::Path:  item.fieldType = 1; break;
            case PopupBlocker::RuleField::Title: item.fieldType = 2; break;
            case PopupBlocker::RuleField::Class: item.fieldType = 3; break;
            }

            switch (r.mode) {
            case PopupBlocker::MatchMode::Contains: item.matchMode = 0; break;
            case PopupBlocker::MatchMode::Exact:    item.matchMode = 1; break;
            case PopupBlocker::MatchMode::Wildcard: item.matchMode = 2; break;
            }

            m_rules.push_back(item);
        }

        // 初始化社区规则开关并触发首次拉取
        CommunityRulesToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"CommunityRulesEnabled", 1) == 1);
        if (CommunityRulesToggle().IsOn()) {
            PopupBlocker::FetchCommunityRulesAsync();
        }

        RefreshList();
    }

    void PopupBlockerPage::RefreshList()
    {
        RulesList().Items().Clear();
        m_visibleIndex.clear();
        for (size_t i = 0; i < m_rules.size(); ++i)
        {
            auto const& r = m_rules[i];
            std::wstring display = std::wstring(ListTypeLabel(r.listType)) + L" | " +
                FieldLabel(r.fieldType) + L" | " +
                MatchModeLabel(r.matchMode) + L"：" + r.pattern;

            // 搜索过滤逻辑
            if (!m_searchText.empty() &&
                PopupBlocker::Lower(display).find(m_searchText) == std::wstring::npos)
                continue;

            m_visibleIndex.push_back(i);
            RulesList().Items().Append(box_value(hstring(display)));
        }
    }

    void PopupBlockerPage::Save()
    {
        std::vector<PopupBlocker::Rule> newRules;
        for (auto const& r : m_rules)
        {
            PopupBlocker::Rule rule;
            rule.isWhitelist = (r.listType == 1);

            switch (r.fieldType) {
            case 0: rule.field = PopupBlocker::RuleField::Exe; break;
            case 1: rule.field = PopupBlocker::RuleField::Path; break;
            case 2: rule.field = PopupBlocker::RuleField::Title; break;
            case 3: rule.field = PopupBlocker::RuleField::Class; break;
            }

            switch (r.matchMode) {
            case 0: rule.mode = PopupBlocker::MatchMode::Contains; break;
            case 1: rule.mode = PopupBlocker::MatchMode::Exact; break;
            case 2: rule.mode = PopupBlocker::MatchMode::Wildcard; break;
            }

            rule.pattern = r.pattern;
            newRules.push_back(rule);
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
            PopupBlocker::FetchCommunityRulesAsync();
        }
        else {
            // 关闭时清空内存中的社区规则
            std::lock_guard lock(PopupBlocker::RulesMutex);
            PopupBlocker::CommunityRules.clear();
        }
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
        for (auto const& r : m_rules)
        {
            if (r.listType != listType &&
                r.fieldType == fieldType &&
                r.matchMode == matchMode &&
                PopupBlocker::Lower(r.pattern) == patternLower)
            {
                conflict = true;
                break;
            }
        }

        m_rules.push_back({ listType, fieldType, matchMode, pattern });
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
}