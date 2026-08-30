#pragma once
#include "pch.h"
#include "BlockLogPage.xaml.h"
#include "PopupBlocker.h"
#include "LabelStorage.h"
#include "FilePicker.h"
#include <sstream>
#include <vector>
#include <algorithm>
#include <ctime>
#include <string>
#if __has_include("BlockLogPage.g.cpp")
#include "BlockLogPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    inline std::wstring GetRelativeTime(std::wstring const& timeStr)
    {
        // 期望格式: "2026-08-29 15:30:12" (19 chars)
        if (timeStr.length() < 19) return L"";

        int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
        if (::swscanf_s(timeStr.c_str(), L"%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6)
            return L"";

        SYSTEMTIME stLog{};
        stLog.wYear = (WORD)y; stLog.wMonth = (WORD)mo; stLog.wDay = (WORD)d;
        stLog.wHour = (WORD)h; stLog.wMinute = (WORD)mi; stLog.wSecond = (WORD)s;

        SYSTEMTIME stNow{};
        ::GetLocalTime(&stNow);

        FILETIME ftLog{}, ftNow{};
        ::SystemTimeToFileTime(&stLog, &ftLog);
        ::SystemTimeToFileTime(&stNow, &ftNow);

        ULARGE_INTEGER t1, t2;
        t1.LowPart = ftLog.dwLowDateTime; t1.HighPart = ftLog.dwHighDateTime;
        t2.LowPart = ftNow.dwLowDateTime; t2.HighPart = ftNow.dwHighDateTime;

        // FILETIME 单位是 100纳秒，除以 10000 得到毫秒
        long long diffMs = (t2.QuadPart - t1.QuadPart) / 10000;
        if (diffMs < 0) diffMs = 0;

        long long sec = diffMs / 1000;
        long long min = sec / 60;
        long long hr = min / 60;
        long long day = hr / 24;

        if (sec < 60) return L" · 刚刚";
        if (min < 60) return L" · " + std::to_wstring(min) + L" 分钟前";
        if (hr < 24) return L" · " + std::to_wstring(hr) + L" 小时前";
        if (day < 30) return L" · " + std::to_wstring(day) + L" 天前";
        return L"";
    }

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

    inline std::wstring FormatLogLineChinese(std::wstring s)
    {
        ReplaceAll(s, L"action=monitor", L"动作=监控");
        ReplaceAll(s, L"action=allow", L"动作=放行");
        ReplaceAll(s, L"action=block", L"动作=拦截");
        ReplaceAll(s, L"action=kill", L"动作=强杀");
        ReplaceAll(s, L"ev=SHOW", L"事件=出现");
        ReplaceAll(s, L"ev=FG", L"事件=焦点");
        ReplaceAll(s, L"reason=heuristic(", L"原因=启发式(");
        ReplaceAll(s, L"reason=whitelist", L"原因=白名单");
        ReplaceAll(s, L"reason=blacklist", L"原因=黑名单");
        ReplaceAll(s, L"reason=heuristic_off", L"原因=启发式关闭");
        ReplaceAll(s, L"infra_class_skip", L"基础设施类名跳过");
        ReplaceAll(s, L"zero_size_skip", L"零尺寸跳过");
        ReplaceAll(s, L"raw=", L"特征=");
        ReplaceAll(s, L"ml=Y", L"ML=是");
        ReplaceAll(s, L"ml=N", L"ML=否");
        ReplaceAll(s, L"title=", L"标题=");
        ReplaceAll(s, L"class=", L"类名=");
        ReplaceAll(s, L"exe=", L"程序=");
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

    void BlockLogPage::ReloadFromFile()
    {
        std::wstringstream ss(ReadLogText());
        std::wstring line;
        m_allLines.clear();
        while (std::getline(ss, line, L'\n'))
        {
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            if (!line.empty()) m_allLines.push_back(line);
        }
    }

    void BlockLogPage::ApplyFilter()
    {
        if (!LogList()) return;
        LogList().Items().Clear();
        m_rawLines.clear();

        std::wstring filterTag = L"all";
        if (FilterCombo() && FilterCombo().SelectedItem()) {
            if (auto item = FilterCombo().SelectedItem().try_as<Controls::ComboBoxItem>()) {
                if (auto tagObj = item.Tag()) {
                    filterTag = winrt::unbox_value<winrt::hstring>(tagObj).c_str();
                }
            }
        }

        std::wstring searchText;
        if (SearchBox()) {
            searchText = hstring(SearchBox().Text()).c_str();
            std::transform(searchText.begin(), searchText.end(), searchText.begin(), ::towlower);
        }

        int threshold = PopupBlocker::HeuristicThreshold;
        int shownCount = 0;
        int totalCount = (int)m_allLines.size();

        for (auto it = m_allLines.rbegin(); it != m_allLines.rend(); ++it)
        {
            const std::wstring& rawLine = *it;

            // 搜索框过滤（大小写不敏感）
            if (!searchText.empty()) {
                std::wstring lowerRaw = rawLine;
                std::transform(lowerRaw.begin(), lowerRaw.end(), lowerRaw.begin(), ::towlower);
                if (lowerRaw.find(searchText) == std::wstring::npos) continue;
            }

            // 类别过滤
            bool passFilter = false;
            if (filterTag == L"all") {
                passFilter = true;
            }
            else if (filterTag == L"list") {
                passFilter = (rawLine.find(L"reason=whitelist") != std::wstring::npos) ||
                    (rawLine.find(L"reason=blacklist") != std::wstring::npos);
            }
            else if (filterTag == L"ml_heur") {
                bool mlY = rawLine.find(L" ml=Y") != std::wstring::npos;
                bool mlN = rawLine.find(L" ml=N") != std::wstring::npos;
                if (mlY || mlN) {
                    bool hasScore = false;
                    int score = 0;
                    auto heurPos = rawLine.find(L"heuristic(");
                    if (heurPos != std::wstring::npos) {
                        size_t endPos = rawLine.find(L')', heurPos);
                        if (endPos != std::wstring::npos) {
                            try {
                                score = std::stoi(rawLine.substr(heurPos + 10, endPos - heurPos - 10));
                                hasScore = true;
                            }
                            catch (...) {}
                        }
                    }

                    if (hasScore) {
                        bool heurSaysPopup = (score >= threshold);
                        bool mlSaysPopup = mlY;
                        if (heurSaysPopup != mlSaysPopup) passFilter = true;
                    }
                }
            }
            else if (filterTag == L"ml_list") {
                bool mlY = rawLine.find(L" ml=Y") != std::wstring::npos;
                bool isWhitelist = rawLine.find(L"reason=whitelist") != std::wstring::npos;
                bool isBlacklist = rawLine.find(L"reason=blacklist") != std::wstring::npos;
                if ((isWhitelist && mlY) || (isBlacklist && !mlY)) passFilter = true;
            }

            if (!passFilter) continue;

            std::wstring display = FormatLogLineChinese(rawLine);
            if (display.length() >= 19) {
                std::wstring relTime = GetRelativeTime(display.substr(0, 19));
                if (!relTime.empty()) {
                    display.insert(19, relTime);
                }
            }
            if (auto labelIt = m_labels.find(rawLine); labelIt != m_labels.end()) {
                if (labelIt->second.label == L"popup") display = L"[弹窗] " + display;
                else if (labelIt->second.label == L"notpopup") display = L"[误关] " + display;
            }
            m_rawLines.push_back(rawLine);
            LogList().Items().Append(box_value(hstring(display)));
            shownCount++;
        }

        if (FilterCountText()) {
            FilterCountText().Text(hstring(std::to_wstring(shownCount) + L" / " + std::to_wstring(totalCount) + L" 条"));
        }
    }

    void BlockLogPage::Load()
    {
        uint64_t t = LogWriteTime();
        if (t != m_lastWrite) {
            m_lastWrite = t;
            ReloadFromFile();
        }
        SampleLabels::Load(m_labels);
        ApplyFilter();
    }

    void BlockLogPage::Filter_Changed(IInspectable const&, Controls::SelectionChangedEventArgs const&)
    {
        ApplyFilter();
    }

    void BlockLogPage::Search_Changed(IInspectable const&, Controls::TextChangedEventArgs const&)
    {
        ApplyFilter();
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

    BlockLogPage::~BlockLogPage()
    {
        if (m_timer) m_timer.Stop();
    }
}