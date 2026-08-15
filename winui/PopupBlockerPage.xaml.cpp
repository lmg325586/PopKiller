#include "pch.h"
#include "PopupBlockerPage.xaml.h"
#include "AppSettings.h"
#include "PopupBlocker.h"
#include <sstream>
#if __has_include("PopupBlockerPage.g.cpp")
#include "PopupBlockerPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    const wchar_t* TypeKey(int idx)
    {
        switch (idx)
        {
        case 0:  return L"exe";
        case 1:  return L"title";
        default: return L"class";
        }
    }

    const wchar_t* TypeLabel(int idx)
    {
        switch (idx)
        {
        case 0:  return L"进程";
        case 1:  return L"标题";
        default: return L"类名";
        }
    }
}

namespace winrt::winui::implementation
{
    PopupBlockerPage::PopupBlockerPage()
    {
        InitializeComponent();

        EnableToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1);

        int count = AppSettings::ReadInt(L"Blocker", L"RuleCount", 0);
        for (int i = 0; i < count; ++i)
        {
            std::wstring line = AppSettings::ReadString(
                L"Blocker", (L"Rule" + std::to_wstring(i)).c_str());
            auto pos = line.find(L':');
            if (pos == std::wstring::npos) continue;
            std::wstring t = line.substr(0, pos);
            int idx = (t == L"exe") ? 0 : (t == L"title") ? 1 : 2;
            m_rules.push_back({ idx, line.substr(pos + 1) });
        }
        RefreshList();
    }

    void PopupBlockerPage::RefreshList()
    {
        RulesList().Items().Clear();
        for (auto const& r : m_rules)
        {
            RulesList().Items().Append(box_value(hstring(
                std::wstring(TypeLabel(r.first)) + L"：" + r.second)));
        }
    }

    void PopupBlockerPage::Save()
    {
        int oldCount = AppSettings::ReadInt(L"Blocker", L"RuleCount", 0);

        int i = 0;
        for (auto const& r : m_rules)
        {
            AppSettings::WriteString(L"Blocker",
                (L"Rule" + std::to_wstring(i)).c_str(),
                std::wstring(TypeKey(r.first)) + L":" + r.second);
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

        m_rules.push_back({ RuleTypeCombo().SelectedIndex(), std::wstring(text) });
        PatternInput().Text(L"");
        Save();
        PopupBlocker::SyncFromSettings();
        RefreshList();
    }

    void PopupBlockerPage::DeleteRule_Click(IInspectable const&, RoutedEventArgs const&)
    {
        int idx = RulesList().SelectedIndex();
        if (idx < 0 || idx >= static_cast<int>(m_rules.size())) return;

        m_rules.erase(m_rules.begin() + idx);
        Save();
        PopupBlocker::SyncFromSettings();
        RefreshList();
    }
}