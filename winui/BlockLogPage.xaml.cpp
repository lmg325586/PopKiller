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

    inline void ReplaceAll(std::wstring& s, const wchar_t* from, const wchar_t* to)
    {
        std::wstring f = from, t = to;
        size_t p = 0;
        while ((p = s.find(f, p)) != std::wstring::npos) { s.replace(p, f.size(), t); p += t.size(); }
    }

    inline void TranslateTokenName(std::wstring& s, const wchar_t* en, const wchar_t* zh)
    {
        std::wstring f = en, t = zh;
        size_t p = 0;
        while ((p = s.find(f, p)) != std::wstring::npos) {
            bool leftOk = (p == 0) || s[p - 1] == L' ';
            size_t e = p + f.size();
            bool rightOk = e < s.size() && (s[e] == L'+' || s[e] == L'-');
            if (leftOk && rightOk) { s.replace(p, f.size(), t); p += t.size(); }
            else p = e;
        }
    }

    // 显示层翻译：blocklog.txt 存储格式与 ParseLine 契约保持英文不动
    inline std::wstring FormatLogLineChinese(std::wstring s)
    {
        ReplaceAll(s, L"action=monitor", L"动作=监控");
        ReplaceAll(s, L"action=block", L"动作=拦截");
        ReplaceAll(s, L"action=kill", L"动作=强杀");
        ReplaceAll(s, L"ev=SHOW", L"事件=出现");
        ReplaceAll(s, L"ev=FG", L"事件=焦点");
        ReplaceAll(s, L"reason=heuristic(", L"原因=启发式(");
        ReplaceAll(s, L"infra_class_skip", L"基础设施类名跳过");
        ReplaceAll(s, L"zero_size_skip", L"零尺寸跳过");
        ReplaceAll(s, L"raw=", L"特征=");
        ReplaceAll(s, L"ml=Y", L"ML=是");
        ReplaceAll(s, L"ml=N", L"ML=否");
        ReplaceAll(s, L"title=", L"标题=");
        ReplaceAll(s, L"class=", L"类名=");
        ReplaceAll(s, L"exe=", L"程序=");
        // 启发式明细 token（长名先翻，防前缀误伤）
        TranslateTokenName(s, L"notresizable", L"不可调");
        TranslateTokenName(s, L"nominmax", L"无最小最大化");
        TranslateTokenName(s, L"unsigned", L"无签名");
        TranslateTokenName(s, L"resizable", L"可调");
        TranslateTokenName(s, L"minmax", L"最小最大化");
        TranslateTokenName(s, L"capsys", L"标题栏");
        TranslateTokenName(s, L"notitle", L"无标题");
        TranslateTokenName(s, L"toolwin", L"工具窗");
        TranslateTokenName(s, L"topmost", L"置顶");
        TranslateTokenName(s, L"noact", L"不激活");
        TranslateTokenName(s, L"hexclass", L"十六进制类名");
        TranslateTokenName(s, L"signed", L"有签名");
        TranslateTokenName(s, L"young", L"新进程");
        TranslateTokenName(s, L"roaming", L"漫游目录");
        TranslateTokenName(s, L"owner", L"有属主");
        TranslateTokenName(s, L"small", L"小窗");
        TranslateTokenName(s, L"large", L"大窗");
        TranslateTokenName(s, L"temp", L"临时目录");
        return s;
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
        m_rawLines.clear();
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
            std::wstring display = FormatLogLineChinese(*it);
            if (auto labelIt = m_labels.find(*it); labelIt != m_labels.end())
            {
                if (labelIt->second.label == L"popup")
                    display = L"[弹窗] " + display;
                else if (labelIt->second.label == L"notpopup")
                    display = L"[误关] " + display;
            }
            m_rawLines.push_back(*it);   // 与列表项一一对应，供右键操作取原文
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
            m_selectedRaw.clear();
            uint32_t idx = 0;
            if (LogList().Items().IndexOf(box_value(hstring(m_selectedDisplayText)), idx)
                && idx < m_rawLines.size())
                m_selectedRaw = m_rawLines[idx];
        }
    }

    void BlockLogPage::MarkPopup_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_selectedRaw.empty()) return;
        auto s = SampleLabels::ParseLine(m_selectedRaw);
        s.label = L"popup";
        m_labels[m_selectedRaw] = s;
        SampleLabels::Save(m_labels);
        Load();
    }

    void BlockLogPage::MarkNotPopup_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_selectedRaw.empty()) return;
        auto s = SampleLabels::ParseLine(m_selectedRaw);
        s.label = L"notpopup";
        m_labels[m_selectedRaw] = s;
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
            SampleLabels::Clear(m_labels);
            Load();

            MessageBoxW(nullptr, L"训练数据导出成功，本地缓存已清空", L"提示", MB_OK | MB_ICONINFORMATION);
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
        if (m_selectedRaw.empty()) return;
        auto s = SampleLabels::ParseLine(m_selectedRaw);
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