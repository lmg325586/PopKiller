#include "pch.h"
#include "PopupBlockerPage.xaml.h"
#include "AppSettings.h"
#include "PopupBlocker.h"
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

        int count = AppSettings::ReadInt(L"Blocker", L"RuleCount", 0);
        for (int i = 0; i < count; ++i)
        {
            std::wstring line = AppSettings::ReadString(
                L"Blocker", (L"Rule" + std::to_wstring(i)).c_str());

            RuleItem item{ 0, 0, 0, L"" }; // 默认: 黑名单, 进程, 包含
            size_t p1 = line.find(L':');
            if (p1 == std::wstring::npos) continue;

            std::wstring first = line.substr(0, p1);
            size_t p2 = line.find(L':', p1 + 1);

            if (first == L"B" || first == L"W") {
                item.listType = (first == L"W") ? 1 : 0;
                if (p2 == std::wstring::npos) continue;
                std::wstring f_str = line.substr(p1 + 1, p2 - p1 - 1);
                size_t p3 = line.find(L':', p2 + 1);
                std::wstring m_str, p_str;
                if (p3 == std::wstring::npos) {
                    m_str = L"contains"; p_str = line.substr(p2 + 1);
                }
                else {
                    m_str = line.substr(p2 + 1, p3 - p2 - 1);
                    p_str = line.substr(p3 + 1);
                }

                if (f_str == L"exe") item.fieldType = 0;
                else if (f_str == L"path") item.fieldType = 1;
                else if (f_str == L"title") item.fieldType = 2;
                else if (f_str == L"class") item.fieldType = 3;
                else continue;

                if (m_str == L"exact") item.matchMode = 1;
                else if (m_str == L"wildcard") item.matchMode = 2;
                else item.matchMode = 0;
                item.pattern = p_str;
            }
            else {
                // 兼容旧格式: exe:pattern
                item.listType = 0;
                if (first == L"exe") item.fieldType = 0;
                else if (first == L"title") item.fieldType = 2;
                else if (first == L"class") item.fieldType = 3;
                else continue;
                item.matchMode = 0;
                item.pattern = line.substr(p1 + 1);
            }
            m_rules.push_back(item);
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
        int oldCount = AppSettings::ReadInt(L"Blocker", L"RuleCount", 0);

        int i = 0;
        for (auto const& r : m_rules)
        {
            std::wstring line = std::wstring(ListTypeKey(r.listType)) + L":" +
                FieldKey(r.fieldType) + L":" +
                MatchModeKey(r.matchMode) + L":" + r.pattern;
            AppSettings::WriteString(L"Blocker",
                (L"Rule" + std::to_wstring(i)).c_str(), line);
            ++i;
        }
        for (; i < oldCount; ++i)
            AppSettings::DeleteKey(L"Blocker", (L"Rule" + std::to_wstring(i)).c_str());

        AppSettings::WriteInt(L"Blocker", L"RuleCount", static_cast<int>(m_rules.size()));
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
        PopupBlocker::SyncFromSettings();
        RefreshList();

        if (conflict)
        {
            PickInfo().Text(L"⚠ 注意：已存在相同内容的相反名单规则；白名单优先，该窗口将被放行。");
            PickInfo().Foreground(Media::SolidColorBrush(
                winrt::Windows::UI::Color{ 0xFF, 0xE6, 0xA2, 0x3C })); // 橙色警告
        }
        else
        {
            PickInfo().Text(L"");
            PickInfo().Foreground(Media::SolidColorBrush(
                winrt::Windows::UI::Color{ 0xFF, 0x80, 0x80, 0x80 })); // 恢复灰
        }
    }

    void PopupBlockerPage::DeleteRule_Click(IInspectable const&, RoutedEventArgs const&)
    {
        int idx = RulesList().SelectedIndex();
        if (idx < 0 || idx >= static_cast<int>(m_visibleIndex.size())) return;

        size_t real = m_visibleIndex[static_cast<size_t>(idx)];
        m_rules.erase(m_rules.begin() + real);
        Save();
        PopupBlocker::SyncFromSettings();
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