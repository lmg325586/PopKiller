#pragma once
#include "pch.h"
#include "BlockLogPage.xaml.h"
#include "PopupBlocker.h"
#include "LabelStorage.h"
#include "FilePicker.h"
#include <sstream>
#include <vector>
#include <algorithm>
#include <functional>
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

    // 增量读取：从 offset 起读，只消费到最后一个 \n（半行防护）
    std::wstring ReadLogTextFrom(uint64_t offset, uint64_t& consumed)
    {
        consumed = 0;
        HANDLE hf = ::CreateFileW(PopupBlocker::LogPath().c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return {};
        LARGE_INTEGER pos{}; pos.QuadPart = static_cast<LONGLONG>(offset);
        if (!::SetFilePointerEx(hf, pos, nullptr, FILE_BEGIN)) { ::CloseHandle(hf); return {}; }

        std::string buf;
        char chunk[65536];
        DWORD rd{};
        while (::ReadFile(hf, chunk, sizeof(chunk), &rd, nullptr) && rd > 0)
            buf.append(chunk, rd);
        ::CloseHandle(hf);

        size_t lastNl = buf.rfind('\n');
        if (lastNl == std::string::npos) return {};
        buf.resize(lastNl + 1);
        consumed = offset + buf.size();

        int need = ::MultiByteToWideChar(CP_UTF8, 0, buf.c_str(), -1, nullptr, 0);
        if (need <= 0) return {};
        std::wstring wide(static_cast<size_t>(need) - 1, 0);
        ::MultiByteToWideChar(CP_UTF8, 0, buf.c_str(), -1, wide.data(), need);
        if (!wide.empty() && wide[0] == 0xFEFF) wide.erase(0, 1); // offset==0 时的 BOM
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
        ReplaceAll(s, L"ml=-", L"ML=跳过");
        ReplaceAll(s, L"title=", L"标题=");
        ReplaceAll(s, L"class=", L"类名=");
        ReplaceAll(s, L"exe=", L"程序=");
        TranslateTokenName(s, L"mouse_close", L"靠近鼠标");
        TranslateTokenName(s, L"idle", L"用户空闲");
        TranslateTokenName(s, L"far_mouse", L"远离鼠标");
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

        this->NavigationCacheMode(Navigation::NavigationCacheMode::Disabled);

        Load();

        m_timer = DispatcherTimer();
        m_timer.Interval(std::chrono::seconds(1));
        m_timer.Tick({ get_weak(), &BlockLogPage::Timer_Tick });
        m_timer.Start();

        this->Unloaded([this](auto&&, auto&&)
            {
                if (m_timer) m_timer.Stop();
                PopupBlocker::BlockOccurredCallback = nullptr;
            });
    }

    void BlockLogPage::OnLogListLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        std::function<DependencyObject(DependencyObject)> walk =
            [&](DependencyObject d) -> DependencyObject {
                if (!d) return nullptr;
                if (auto sv = d.try_as<Controls::ScrollViewer>()) return sv;
                int n = Media::VisualTreeHelper::GetChildrenCount(d);
                for (int i = 0; i < n; ++i) {
                    auto r = walk(Media::VisualTreeHelper::GetChild(d, i));
                    if (r) return r;
                }
                return nullptr;
            };
        auto found = walk(LogList());
        m_logScrollViewer = found ? found.try_as<Controls::ScrollViewer>() : nullptr;

        if (m_logScrollViewer) {
            m_logScrollViewer.ViewChanged([this](IInspectable const&,
                Controls::ScrollViewerViewChangedEventArgs const&)
                {
                    if (m_inApplyFilter || !m_logScrollViewer) return;
                    bool nowPinned = m_logScrollViewer.VerticalOffset() < 4.0;
                    if (nowPinned && !m_pinnedToTop) {
                        // 回到顶部：flush 缓冲的新日志
                        m_pendingNewCount = 0;
                        SyncJumpButton();
                        ApplyFilter();
                    }
                    m_pinnedToTop = nowPinned;
                });
        }
    }

    std::wstring BlockLogPage::CurrentFilterTag()
    {
        std::wstring filterTag = L"all";
        if (FilterCombo() && FilterCombo().SelectedItem()) {
            if (auto item = FilterCombo().SelectedItem().try_as<Controls::ComboBoxItem>()) {
                if (auto tagObj = item.Tag()) {
                    filterTag = winrt::unbox_value<winrt::hstring>(tagObj).c_str();
                }
            }
        }
        return filterTag;
    }

    std::wstring BlockLogPage::CurrentSearchText()
    {
        std::wstring searchText;
        if (SearchBox()) {
            searchText = hstring(SearchBox().Text()).c_str();
            std::transform(searchText.begin(), searchText.end(), searchText.begin(), ::towlower);
        }
        return searchText;
    }

    bool BlockLogPage::LinePassFilter(std::wstring const& rawLine, std::wstring const& filterTag,
        std::wstring const& searchText, int threshold)
    {
        if (!searchText.empty()) {
            std::wstring lowerRaw = rawLine;
            std::transform(lowerRaw.begin(), lowerRaw.end(), lowerRaw.begin(), ::towlower);
            if (lowerRaw.find(searchText) == std::wstring::npos) return false;
        }

        if (filterTag == L"all") return true;

        if (filterTag == L"list") {
            return (rawLine.find(L"reason=whitelist") != std::wstring::npos) ||
                (rawLine.find(L"reason=blacklist") != std::wstring::npos);
        }

        if (filterTag == L"ml_heur") {
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
                    if (heurSaysPopup != mlSaysPopup) return true;
                }
            }
            return false;
        }

        if (filterTag == L"ml_list") {
            bool mlY = rawLine.find(L" ml=Y") != std::wstring::npos;
            bool isWhitelist = rawLine.find(L"reason=whitelist") != std::wstring::npos;
            bool isBlacklist = rawLine.find(L"reason=blacklist") != std::wstring::npos;
            if ((isWhitelist && mlY) || (isBlacklist && !mlY)) return true;
            return false;
        }

        return false;
    }

    std::wstring BlockLogPage::BuildDisplay(std::wstring const& raw)
    {
        std::wstring display = FormatLogLineChinese(raw);
        if (display.length() >= 19) {
            std::wstring relTime = GetRelativeTime(display.substr(0, 19));
            if (!relTime.empty()) display.insert(19, relTime);
        }
        if (auto labelIt = m_labels.find(raw); labelIt != m_labels.end()) {
            if (labelIt->second.label == L"popup") display = L"[弹窗] " + display;
            else if (labelIt->second.label == L"notpopup") display = L"[误关] " + display;
        }
        return display;
    }

    void BlockLogPage::SyncJumpButton()
    {
        if (!NewLogJumpButton()) return;
        if (m_pendingNewCount == 0) {
            NewLogJumpButton().Visibility(Visibility::Collapsed);
        }
        else {
            NewLogJumpButton().Content(box_value(hstring(
                L"↑ 有新日志 (" + std::to_wstring(m_pendingNewCount) + L")")));
            NewLogJumpButton().Visibility(Visibility::Visible);
        }
    }

    void BlockLogPage::UpdateCountText()
    {
        if (FilterCountText()) {
            FilterCountText().Text(hstring(std::to_wstring(m_rawLines.size()) +
                L" / " + std::to_wstring(m_allLines.size()) + L" 条"));
        }
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
        m_inApplyFilter = true;
        LogList().Items().Clear();
        m_rawLines.clear();

        std::wstring filterTag = CurrentFilterTag();
        std::wstring searchText = CurrentSearchText();
        int threshold = PopupBlocker::HeuristicThreshold;

        for (auto it = m_allLines.rbegin(); it != m_allLines.rend(); ++it)
        {
            const std::wstring& rawLine = *it;
            if (!LinePassFilter(rawLine, filterTag, searchText, threshold)) continue;
            m_rawLines.push_back(rawLine);
            LogList().Items().Append(box_value(hstring(BuildDisplay(rawLine))));
        }

        UpdateCountText();
        m_pendingNewCount = 0;
        SyncJumpButton();
        m_inApplyFilter = false;
    }

    void BlockLogPage::AppendNewLines()
    {
        uint64_t consumed = 0;
        std::wstring text = ReadLogTextFrom(m_lastFileSize, consumed);
        if (consumed > m_lastFileSize) m_lastFileSize = consumed;
        if (text.empty()) return;

        std::vector<std::wstring> newLines; // 文件顺序：旧 → 新
        size_t start = 0;
        while (start < text.size()) {
            size_t nl = text.find(L'\n', start);
            if (nl == std::wstring::npos) nl = text.size();
            std::wstring line = text.substr(start, nl - start);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            start = nl + 1;
            if (!line.empty()) newLines.push_back(line);
        }
        if (newLines.empty()) return;

        for (auto& l : newLines) m_allLines.push_back(l);

        std::wstring filterTag = CurrentFilterTag();
        std::wstring searchText = CurrentSearchText();
        int threshold = PopupBlocker::HeuristicThreshold;

        if (m_pinnedToTop) {
            // 跟随模式：逆序插到头部，保持最新在上
            uint32_t appended = 0;
            for (auto it = newLines.rbegin(); it != newLines.rend(); ++it) {
                if (LinePassFilter(*it, filterTag, searchText, threshold)) {
                    m_rawLines.insert(m_rawLines.begin(), *it);
                    LogList().Items().InsertAt(0, box_value(hstring(BuildDisplay(*it))));
                    ++appended;
                }
            }
            if (appended > 0 && LogList().Items().Size() > 0)
                LogList().ScrollIntoView(LogList().Items().GetAt(0));
        }
        else {
            // 阅读模式：不碰列表，只计数
            uint32_t pass = 0;
            for (auto& l : newLines)
                if (LinePassFilter(l, filterTag, searchText, threshold)) ++pass;
            if (pass > 0) {
                m_pendingNewCount += pass;
                SyncJumpButton();
            }
        }
        UpdateCountText();
    }

    void BlockLogPage::Load()
    {
        uint64_t t = LogWriteTime();
        if (t != m_lastWrite) {
            m_lastWrite = t;
            ReloadFromFile();
            SampleLabels::Load(m_labels);
            ApplyFilter();
        }
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (::GetFileAttributesExW(PopupBlocker::LogPath().c_str(), GetFileExInfoStandard, &fad))
            m_lastFileSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    }

    void BlockLogPage::Filter_Changed(IInspectable const&, Controls::SelectionChangedEventArgs const&)
    {
        ApplyFilter();
    }

    void BlockLogPage::Search_Changed(IInspectable const&, Controls::TextChangedEventArgs const&)
    {
        ApplyFilter();
    }

    void BlockLogPage::OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&)
    {
        Load();
    }

    void BlockLogPage::OnNavigatedFrom(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&)
    {
        if (m_timer) m_timer.Stop();
        PopupBlocker::BlockOccurredCallback = nullptr;
    }

    void BlockLogPage::Timer_Tick(IInspectable const&, IInspectable const&)
    {
        if (PopupBlocker::ShuttingDown.load()) return;
        auto strongThis = get_strong();
        if (!strongThis) return;

        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!::GetFileAttributesExW(PopupBlocker::LogPath().c_str(), GetFileExInfoStandard, &fad)) return;
        uint64_t size = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        ULARGE_INTEGER u{};
        u.LowPart = fad.ftLastWriteTime.dwLowDateTime;
        u.HighPart = fad.ftLastWriteTime.dwHighDateTime;
        uint64_t wt = u.QuadPart;

        if (wt == m_lastWrite && size == m_lastFileSize) return;   // 无变化：零 IO
        if (size < m_lastFileSize) { m_lastWrite = wt; Load(); return; } // 轮转/清空：全量重建
        m_lastWrite = wt;
        AppendNewLines();                                          // 常规：增量
    }

    void BlockLogPage::NewLogJumpButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_pendingNewCount = 0;
        SyncJumpButton();
        m_pinnedToTop = true;
        ApplyFilter();
        if (LogList().Items().Size() > 0)
            LogList().ScrollIntoView(LogList().Items().GetAt(0));
    }

    void BlockLogPage::Refresh_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_lastWrite = 0;
        Load();
    }

    void BlockLogPage::Clear_Click(IInspectable const&, RoutedEventArgs const&)
    {
        FILE* f{};
        if (_wfopen_s(&f, PopupBlocker::LogPath().c_str(), L"wb") == 0 && f)
            ::fclose(f);
        m_lastWrite = 0;
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
        PopupBlocker::BlockOccurredCallback = nullptr;
    }
}