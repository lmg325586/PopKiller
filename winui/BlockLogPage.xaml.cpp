#include "pch.h"
#include "BlockLogPage.xaml.h"
#include "PopupBlocker.h"
#include "LabelStorage.h"
#include "FilePicker.h"
#include <sstream>
#include <vector>
#if __has_include("BlockLogPage.g.cpp")
#include "BlockLogPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    std::wstring ReadLogText()
    {
        FILE* f{};
        if (_wfopen_s(&f, PopupBlocker::LogPath().c_str(), L"rb") != 0 || !f) return {};
        std::string data;
        char buf[4096];
        size_t n;
        while ((n = ::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
        ::fclose(f);

        if (data.size() >= 3 &&
            (unsigned char)data[0] == 0xEF &&
            (unsigned char)data[1] == 0xBB &&
            (unsigned char)data[2] == 0xBF)
            data.erase(0, 3);

        int need = ::MultiByteToWideChar(CP_UTF8, 0, data.c_str(), -1, nullptr, 0);
        if (need <= 0) return {};
        std::wstring wide(static_cast<size_t>(need) - 1, 0);
        ::MultiByteToWideChar(CP_UTF8, 0, data.c_str(), -1, wide.data(), need);
        return wide;
    }

    uint64_t LogWriteTime()
    {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (!::GetFileAttributesExW(PopupBlocker::LogPath().c_str(),
            GetFileExInfoStandard, &a))
            return 0;
        ULARGE_INTEGER u{};
        u.LowPart = a.ftLastWriteTime.dwLowDateTime;
        u.HighPart = a.ftLastWriteTime.dwHighDateTime;
        return u.QuadPart;
    }

    std::wstring GetRawLine(std::wstring const& displayText)
    {
        if (displayText.find(L"[弹窗] ") == 0) return displayText.substr(5);
        if (displayText.find(L"[误关] ") == 0) return displayText.substr(5);
        return displayText;
    }
}

namespace winrt::winui::implementation
{
    BlockLogPage::BlockLogPage()
    {
        InitializeComponent();
        Load();

        m_timer = DispatcherTimer();
        m_timer.Interval(std::chrono::seconds(1));
        m_timer.Tick({ this, &BlockLogPage::Timer_Tick });
        m_timer.Start();

        this->Unloaded([this](auto&&, auto&&)
            {
                if (m_timer) m_timer.Stop();
            });
    }

    void BlockLogPage::Load()
    {
        LogList().Items().Clear();
        SampleLabels::Load(m_labels);

        std::wstringstream ss(ReadLogText());
        std::wstring line;
        std::vector<std::wstring> lines;
        while (std::getline(ss, line, L'\n'))
        {
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
        {
            std::wstring display = *it;
            if (auto labelIt = m_labels.find(*it); labelIt != m_labels.end())
            {
                if (labelIt->second.label == L"popup")
                    display = L"[弹窗] " + *it;
                else if (labelIt->second.label == L"notpopup")
                    display = L"[误关] " + *it;
            }
            LogList().Items().Append(box_value(hstring(display)));
        }

        m_lastWrite = LogWriteTime();
    }

    void BlockLogPage::Timer_Tick(IInspectable const&, IInspectable const&)
    {
        uint64_t t = LogWriteTime();
        if (t != m_lastWrite)
        {
            m_lastWrite = t;
            Load();
        }
    }

    void BlockLogPage::Refresh_Click(IInspectable const&, RoutedEventArgs const&)
    {
        Load();
    }

    void BlockLogPage::Clear_Click(IInspectable const&, RoutedEventArgs const&)
    {
        FILE* f{};
        if (_wfopen_s(&f, PopupBlocker::LogPath().c_str(), L"wb") == 0 && f)
            ::fclose(f);
        Load();
    }

    void BlockLogPage::LogItem_RightTapped(IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const&)
    {
        if (auto tb = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::TextBlock>())
        {
            m_selectedDisplayText = tb.Text();
        }
    }

    void BlockLogPage::MarkPopup_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_selectedDisplayText.empty()) return;
        std::wstring raw = GetRawLine(m_selectedDisplayText);
        auto s = SampleLabels::ParseLine(raw);
        s.label = L"popup";
        m_labels[raw] = s;
        SampleLabels::Save(m_labels);
        Load();
    }

    void BlockLogPage::MarkNotPopup_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_selectedDisplayText.empty()) return;
        std::wstring raw = GetRawLine(m_selectedDisplayText);
        auto s = SampleLabels::ParseLine(raw);
        s.label = L"notpopup";
        m_labels[raw] = s;
        SampleLabels::Save(m_labels);
        Load();
    }

    void BlockLogPage::ExportSamples_Click(IInspectable const&, RoutedEventArgs const&)
    {
        std::wstring path = FilePicker::PickJsonFile(true);
        if (path.empty()) return;
        std::string json = SampleLabels::ExportJson(m_labels);
        if (PopupBlocker::WriteUtf8StringToFile(path, json))
        {
            MessageBoxW(nullptr, L"训练数据导出成功", L"提示", MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            MessageBoxW(nullptr, L"导出失败", L"提示", MB_OK | MB_ICONERROR);
        }
    }

    void BlockLogPage::AddToBlacklist_Click(IInspectable const&, RoutedEventArgs const&)
    {
        AddRuleFromSelection(false);
    }

    void BlockLogPage::AddToWhitelist_Click(IInspectable const&, RoutedEventArgs const&)
    {
        AddRuleFromSelection(true);
    }

    void BlockLogPage::AddRuleFromSelection(bool whitelist)
    {
        if (m_selectedDisplayText.empty()) return;
        std::wstring raw = GetRawLine(m_selectedDisplayText);
        auto s = SampleLabels::ParseLine(raw);
        if (s.exe.empty())
        {
            MessageBoxW(nullptr, L"该日志缺少进程信息，无法生成规则。",
                L"提示", MB_OK | MB_ICONWARNING);
            return;
        }

        PopupBlocker::Rule r;
        r.isWhitelist = whitelist;
        r.field = PopupBlocker::RuleField::Exe;
        r.mode = PopupBlocker::MatchMode::Exact;
        r.pattern = PopupBlocker::Lower(s.exe);
        r.fromCommunity = false;

        std::vector<PopupBlocker::Rule> rules;
        {
            std::lock_guard lock(PopupBlocker::RulesMutex);
            rules = PopupBlocker::Rules;
        }

        std::wstring k = PopupBlocker::RuleKey(r);
        bool exists = std::any_of(rules.begin(), rules.end(),
            [&k](PopupBlocker::Rule const& e) { return PopupBlocker::RuleKey(e) == k; });
        if (exists)
        {
            MessageBoxW(nullptr, L"相同规则已存在。", L"提示", MB_OK | MB_ICONINFORMATION);
            return;
        }

        rules.push_back(r);
        PopupBlocker::SaveRules(rules);

        std::wstring msg = (whitelist ? L"已添加白名单规则：进程 " : L"已添加黑名单规则：进程 ") + s.exe;
        MessageBoxW(nullptr, msg.c_str(), L"提示", MB_OK | MB_ICONINFORMATION);
    }
}