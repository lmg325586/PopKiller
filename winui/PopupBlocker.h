#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <algorithm>
#include "HeuristicML.h"
#include "AppSettings.h"
#include "HeuristicScorer.h"
#include "RuleTypes.h"
#include "RuleStorage.h"
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Foundation.h>
#include <cstdio>
#include <cwchar>

namespace PopupBlocker
{
    inline std::vector<Rule> Rules;
    inline std::vector<std::wstring> CommunityRemoved;
    inline std::function<void(bool, std::wstring)> CommunityRulesFetchCallback;
    inline std::mutex RulesMutex;
    inline std::atomic<bool> Running{ false };
    inline std::function<void()> EnabledChangedCallback;
    inline bool ForceBlock = false;
    inline std::wstring SelfExe;
    inline bool ToastNotify = true;

    inline std::atomic<bool> Paused{ false };
    inline std::atomic<bool> ShuttingDown{ false };
    inline std::atomic<long long> PauseDeadlineMs{ 0 };
    inline std::atomic<int> PauseGen{ 0 };

    inline int HeuristicMode = 0;
    inline int HeuristicThreshold = 70;
    inline bool VerboseLog = false;
    inline bool MLHeuristic = false;

    inline std::function<void(std::wstring const& exeName, std::wstring const& windowTitle, int matchResult)> BlockOccurredCallback;

    inline long long NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    inline std::wstring LogPath()
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path);
        auto pos = p.find_last_of(L"\\/");
        p = p.substr(0, pos + 1) + L"blocklog.txt";
        return p;
    }

    inline bool WildcardMatch(const wchar_t* str, const wchar_t* pat) {
        const wchar_t* s = str, * p = pat;
        const wchar_t* star_s = nullptr, * star_p = nullptr;
        while (*s) {
            if (*p == L'?' || ::towlower(*p) == ::towlower(*s)) { s++; p++; }
            else if (*p == L'*') { star_p = p++; star_s = s; }
            else if (star_p) { p = star_p + 1; s = ++star_s; }
            else return false;
        }
        while (*p == L'*') p++;
        return *p == L'\0';
    }

    inline void InitSelfExe()
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path);
        auto pos = p.find_last_of(L"\\/");
        SelfExe = Lower((pos == std::wstring::npos) ? p : p.substr(pos + 1));
    }

    inline bool LooksLikePopup(HWND hwnd)
    {
        LONG style = ::GetWindowLongW(hwnd, GWL_STYLE);
        LONG ex = ::GetWindowLongW(hwnd, GWL_EXSTYLE);

        if (::GetWindow(hwnd, GW_OWNER)) return true;
        if (ex & WS_EX_TOOLWINDOW) return true;

        bool resizable = (style & WS_THICKFRAME) != 0;
        bool hasMinMax = (style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) != 0;
        if (!resizable && !hasMinMax) return true;

        return false;
    }

    inline void SaveRules(std::vector<Rule> const& newRules)
    {
        SaveRulesJson(newRules, CommunityRemoved);
        std::lock_guard lock(RulesMutex);
        Rules = newRules;
    }

    inline bool AddWhitelistExe(std::wstring const& exe)
    {
        Rule r;
        r.isWhitelist = true;
        r.field = RuleField::Exe;
        r.mode = MatchMode::Exact;
        r.pattern = Lower(exe);

        std::vector<Rule> rules;
        { std::lock_guard lock(RulesMutex); rules = Rules; }

        std::wstring k = RuleKey(r);
        if (std::any_of(rules.begin(), rules.end(),
            [&](Rule const& e) { return RuleKey(e) == k; })) return false;

        rules.push_back(r);
        SaveRules(rules);
        return true;
    }

    inline void SyncFromSettings()
    {
        ForceBlock = AppSettings::ReadInt(L"Blocker", L"ForceBlock", 0) == 1;
        HeuristicMode = AppSettings::ReadInt(L"Blocker", L"HeuristicMode", 0);
        HeuristicThreshold = AppSettings::ReadInt(L"Blocker", L"HeuristicThreshold", 70);
        VerboseLog = AppSettings::ReadInt(L"Blocker", L"VerboseLog", 0) == 1;
        MLHeuristic = AppSettings::ReadInt(L"Blocker", L"MLHeuristic", 0) == 1;
        ToastNotify = AppSettings::ReadInt(L"Blocker", L"ToastNotify", 1) == 1;

        EnsureDefaultRules();
        std::vector<Rule> rules;
        std::vector<std::wstring> removed;
        LoadRulesJson(rules, removed);
        std::lock_guard lock(RulesMutex);
        Rules = std::move(rules);
        CommunityRemoved = std::move(removed);
    }

    inline winrt::Windows::Foundation::IAsyncAction FetchCommunityRulesAsync()
    {
        using namespace winrt::Windows::Web::Http;
        bool ok = false;
        std::wstring msg;
        try {
            HttpClient client;
            std::wstring url = L"https://raw.githubusercontent.com/lmg325586/PopKiller/master/community_rules.json?t="
                + std::to_wstring(::GetTickCount64());
            winrt::Windows::Foundation::Uri uri(url);

            HttpResponseMessage response = co_await client.GetAsync(uri);
            if (response.StatusCode() != winrt::Windows::Web::Http::HttpStatusCode::Ok) {
                msg = L"HTTP " + std::to_wstring(static_cast<int>(response.StatusCode()));
            }
            else {
                std::string body = winrt::to_string(co_await response.Content().ReadAsStringAsync());
                std::vector<Rule> fetched;
                if (ParseRulesFromJsonString(body, fetched)) {
                    for (auto& r : fetched) r.fromCommunity = true;

                    std::vector<Rule> merged;
                    std::vector<std::wstring> removed;
                    {
                        std::lock_guard lock(RulesMutex);
                        merged = Rules;
                        removed = CommunityRemoved;
                    }

                    size_t added = 0;
                    for (auto& cr : fetched) {
                        std::wstring k = RuleKey(cr);
                        bool gone = std::find(removed.begin(), removed.end(), k) != removed.end();
                        bool exists = std::any_of(merged.begin(), merged.end(),
                            [&](Rule const& r) { return RuleKey(r) == k; });
                        if (!gone && !exists) { merged.push_back(cr); ++added; }
                    }

                    if (added > 0) SaveRules(merged);
                    ok = true;
                    msg = std::to_wstring(added);
                }
                else {
                    msg = L"JSON 解析失败";
                }
            }
        }
        catch (...) {
            msg = L"网络错误";
        }

        if (CommunityRulesFetchCallback) CommunityRulesFetchCallback(ok, msg);
    }

    inline void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

    namespace detail
    {
        inline std::thread Worker;
        inline HWINEVENTHOOK HookShow{};
        inline HWINEVENTHOOK HookFg{};

        inline std::wstring GetProcessName(HWND hwnd)
        {
            DWORD pid{};
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (!pid) return {};
            HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) return {};
            WCHAR path[MAX_PATH]{};
            DWORD size = MAX_PATH;
            std::wstring name;
            if (::QueryFullProcessImageNameW(h, 0, path, &size))
            {
                std::wstring p = path;
                auto pos = p.find_last_of(L"\\/");
                name = (pos == std::wstring::npos) ? p : p.substr(pos + 1);
            }
            ::CloseHandle(h);
            return Lower(name);
        }

        inline std::wstring GetProcessPath(HWND hwnd)
        {
            DWORD pid{};
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (!pid) return {};
            HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) return {};
            WCHAR path[MAX_PATH]{};
            DWORD size = MAX_PATH;
            std::wstring result;
            if (::QueryFullProcessImageNameW(h, 0, path, &size)) result = path;
            ::CloseHandle(h);
            return Lower(result);
        }

        inline bool IsProtected(HWND hwnd)
        {
            std::wstring exe = GetProcessName(hwnd);
            if (!SelfExe.empty() && exe == SelfExe) return true;

            static const wchar_t* list[] = {
                // 外壳与桌面
                L"explorer.exe", L"dwm.exe", L"sihost.exe",
                L"shellexperiencehost.exe", L"startmenuexperiencehost.exe",
                L"searchui.exe", L"searchhost.exe", L"searchapp.exe",
                L"lockapp.exe", L"applicationframehost.exe", L"backgroundtaskhost.exe",
                L"runtimebroker.exe", L"credentialuibroker.exe", L"consent.exe",
                L"peopleexperiencehost.exe",
                // 登录与安全
                L"winlogon.exe", L"logonui.exe", L"smartscreen.exe", L"securityhealthsystray.exe",
                // 输入法与辅助功能
                L"ctfmon.exe", L"textinputhost.exe", L"tabtip.exe", L"osk.exe",
                L"narrator.exe", L"magnify.exe", L"sethc.exe", L"utilman.exe",
                // 系统工具与对话框
                L"taskmgr.exe", L"systemsettings.exe", L"systemsettingsbroker.exe",
                L"control.exe", L"mmc.exe", L"openwith.exe", L"msiexec.exe",
                L"sndvol.exe", L"snippingtool.exe", L"screensketch.exe",
                L"mstsc.exe", L"conhost.exe", L"shellhost.exe", L"snippingtool.exe",
                L"screensketch.exe", L"mspaint.exe", L"calc.exe",L"svchost.exe", L"services.exe", L"lsass.exe", L"csrss.exe",
                // Win11 小组件
                L"widgets.exe", L"widgetservice.exe",
                //常见软件
                L"msedge.exe",L"windowsterminal.exe",L"chrome.exe",L"firefox.exe",
            };
            for (auto p : list) if (exe == p) return true;
            return false;
        }

        inline std::wstring GetTitle(HWND hwnd) { WCHAR buf[256]{}; ::GetWindowTextW(hwnd, buf, 256); return Lower(buf); }
        inline std::wstring GetClass(HWND hwnd) { WCHAR buf[256]{}; ::GetClassNameW(hwnd, buf, 256); return Lower(buf); }

        inline bool MatchRule(HWND hwnd, const Rule& r, std::wstring& exe, std::wstring& path, std::wstring& title, std::wstring& cls) {
            std::wstring target;
            switch (r.field) {
            case RuleField::Exe: if (exe.empty()) exe = GetProcessName(hwnd); target = exe; break;
            case RuleField::Path: if (path.empty()) path = GetProcessPath(hwnd); target = path; break;
            case RuleField::Title: if (title.empty()) title = GetTitle(hwnd); target = title; break;
            case RuleField::Class: if (cls.empty()) cls = GetClass(hwnd); target = cls; break;
            }
            switch (r.mode) {
            case MatchMode::Exact: return target == r.pattern;
            case MatchMode::Contains: return target.find(r.pattern) != std::wstring::npos;
            case MatchMode::Wildcard: return WildcardMatch(target.c_str(), r.pattern.c_str());
            }
            return false;
        }

        // 0=未命中, 1=白名单, 2=黑名单
        inline int Match(HWND hwnd) {
            std::vector<Rule> rules;
            { std::lock_guard lock(RulesMutex); rules = Rules; }
            if (rules.empty()) return 0;

            std::wstring exe, path, title, cls;
            bool matchedW = false, matchedB = false;
            for (auto const& r : rules) {
                if (MatchRule(hwnd, r, exe, path, title, cls)) {
                    if (r.isWhitelist) matchedW = true; else matchedB = true;
                }
            }
            if (matchedW) return 1;
            if (matchedB) return 2;
            return 0;
        }

        inline void Log(std::wstring const& s)
        {
            std::wstring p = LogPath();
            constexpr long long Limit = 1024 * 1024;

            WIN32_FILE_ATTRIBUTE_DATA fad{};
            bool exists = (::GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad) != FALSE);
            long long size = exists
                ? (static_cast<long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow
                : 0;

            bool fresh = !exists || size > Limit;
            FILE* f{};
            if (_wfopen_s(&f, p.c_str(), fresh ? L"wb" : L"ab") == 0 && f)
            {
                if (fresh) ::fwrite("\xEF\xBB\xBF", 1, 3, f);

                SYSTEMTIME st{}; ::GetLocalTime(&st);
                WCHAR ts[32]{};
                swprintf_s(ts, L"%04d-%02d-%02d %02d:%02d:%02d ",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                std::wstring full = ts + s;

                int need = ::WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (need > 0)
                {
                    std::string utf8(static_cast<size_t>(need) - 1, '\0');
                    ::WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, utf8.data(), need, nullptr, nullptr);
                    utf8 += "\r\n";
                    ::fwrite(utf8.data(), 1, utf8.size(), f);
                }
                ::fclose(f);
            }
        }

        struct EventVerdict {
            std::wstring action = L"monitor";
            std::wstring reason;
            std::wstring detail;
            bool shouldBlock = false;
            bool shouldLog = false;
            int  matchResult = 0;
        };

        inline bool PassEventFilter(HWND hwnd, LONG idObject, LONG idChild)
        {
            if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return false;
            if (!::IsWindowVisible(hwnd)) return false;
            if (::GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
            if (IsProtected(hwnd)) return false;
            return true;
        }

        inline EventVerdict EvaluateWindow(HWND hwnd, DWORD idEventTime)
        {
            EventVerdict v;
            bool isPopup = LooksLikePopup(hwnd);
            v.matchResult = Match(hwnd);

            if (v.matchResult == 1) {
                v.reason = L"whitelist";
                v.action = L"allow";
            }
            else if (v.matchResult == 2) {
                v.reason = L"blacklist";
                if (ForceBlock || isPopup) { v.shouldBlock = true; v.action = L"block"; }
            }
            else if (HeuristicMode > 0) {
                HeuristicScorer::Features f = HeuristicScorer::ExtractFeatures(hwnd);
                int score = HeuristicScorer::ScoreWindow(f, v.detail);

                RECT rc{}; ::GetWindowRect(hwnd, &rc);
                v.detail += L" raw=" + HeuristicScorer::BuildRawBits(f, rc, idEventTime);

                if (MLHeuristic) {
                    v.detail += HeuristicML::GetInstance().Predict(hwnd, idEventTime) ? L" ml=Y" : L" ml=N";
                }

                v.reason = L"heuristic(" + std::to_wstring(score) + L")";
                if (score >= HeuristicThreshold && HeuristicMode == 2) { v.shouldBlock = true; v.action = L"block"; }
            }
            else {
                v.reason = L"heuristic_off";
            }

            v.shouldLog = VerboseLog || v.shouldBlock || v.matchResult == 1 || v.matchResult == 2;
            return v;
        }

        inline void WriteEventLog(HWND hwnd, DWORD idEvent, EventVerdict const& v)
        {
            std::wstring logMsg = L"action=" + v.action +
                L" | ev=" + (idEvent == EVENT_OBJECT_SHOW ? L"SHOW" : L"FG") +
                L" | reason=" + v.reason;
            if (!v.detail.empty()) logMsg += L" | " + v.detail;
            logMsg += L" | title=" + GetTitle(hwnd) +
                L" | class=" + GetClass(hwnd) + L" | exe=" + GetProcessName(hwnd);
            Log(logMsg);
        }

        inline void ScheduleForceKill(HWND hwnd)
        {
            std::thread([hwnd]() {
                ::Sleep(400);
                if (!::IsWindow(hwnd)) return;
                DWORD pid = 0;
                ::GetWindowThreadProcessId(hwnd, &pid);
                if (!pid) return;
                HANDLE hProcess = ::OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
                if (!hProcess) return;
                WCHAR path[MAX_PATH] = {};
                DWORD size = MAX_PATH;
                if (::QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
                    std::wstring p = path;
                    std::transform(p.begin(), p.end(), p.begin(), ::towlower);
                    bool isSystemPath = (p.find(L"c:\\windows\\") == 0) ||
                        (p.find(L"c:\\program files\\") == 0) ||
                        (p.find(L"c:\\program files (x86)\\") == 0);
                    if (!isSystemPath) ::TerminateProcess(hProcess, 0);
                }
                ::CloseHandle(hProcess);
                }).detach();
        }

        inline void EnforceBlock(HWND hwnd, int matchResult)
        {
            ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
            ::ShowWindow(hwnd, SW_HIDE);
            if (matchResult == 2) ScheduleForceKill(hwnd);
        }

        inline DWORD WINAPI ThreadMain(LPVOID) {
            HookShow = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            HookFg = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            MSG msg; while (::GetMessageW(&msg, nullptr, 0, 0) > 0) { ::TranslateMessage(&msg); ::DispatchMessageW(&msg); }
            if (HookShow) ::UnhookWinEvent(HookShow); if (HookFg) ::UnhookWinEvent(HookFg);
            HookShow = HookFg = nullptr; return 0;
        }
    }

    inline void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD idEvent, HWND hwnd,
        LONG idObject, LONG idChild, DWORD, DWORD idEventTime)
    {
        if (!detail::PassEventFilter(hwnd, idObject, idChild)) return;
        detail::EventVerdict v = detail::EvaluateWindow(hwnd, idEventTime);
        if (v.shouldLog) detail::WriteEventLog(hwnd, idEvent, v);
        if (v.shouldBlock) {
            detail::EnforceBlock(hwnd, v.matchResult);

            if (BlockOccurredCallback) {
                BlockOccurredCallback(detail::GetProcessName(hwnd), detail::GetTitle(hwnd), v.matchResult);
            }
        }
    }

    inline void Start() { if (Running.exchange(true)) return; InitSelfExe(); detail::Worker = std::thread([] { detail::ThreadMain(nullptr); }); }
    inline void Stop() { if (!Running.exchange(false)) return; ::PostThreadMessageW(::GetThreadId(detail::Worker.native_handle()), WM_QUIT, 0, 0); if (detail::Worker.joinable()) detail::Worker.join(); }

    inline void PauseForMinutes(int minutes)
    {
        bool wasPaused = Paused.exchange(true);
        if (!wasPaused && Running.load()) Stop();

        PauseDeadlineMs.store(NowMs() + static_cast<long long>(minutes) * 60000);
        int gen = ++PauseGen;

        std::thread([gen]() {
            while (Paused.load() && PauseGen.load() == gen && !ShuttingDown.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (NowMs() >= PauseDeadlineMs.load()) {
                    bool expected = true;
                    if (Paused.compare_exchange_strong(expected, false)) {
                        if (!ShuttingDown.load() && AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1) {
                            Start();
                        }
                    }
                    return;
                }
            }
            }).detach();
    }

    inline void ResumeNow()
    {
        if (Paused.exchange(false)) {
            ++PauseGen;
            if (!ShuttingDown.load() && AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1) {
                Start();
            }
        }
    }
}