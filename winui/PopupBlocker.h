#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <algorithm>
#include "AppSettings.h"
#include <cstdio>
#include <cwchar>

namespace PopupBlocker
{
    enum class RuleField { Exe, Path, Title, Class };
    enum class MatchMode { Contains, Exact, Wildcard };

    struct Rule
    {
        bool isWhitelist = false;
        RuleField field = RuleField::Exe;
        MatchMode mode = MatchMode::Contains;
        std::wstring pattern;
    };

    inline std::vector<Rule> Rules;
    inline std::mutex RulesMutex;
    inline std::atomic<bool> Running{ false };
    inline std::function<void()> EnabledChangedCallback;
    inline bool ForceBlock = false;
    inline std::wstring SelfExe;

    // 启发式配置缓存（避免在钩子线程中频繁读取 INI）
    inline int HeuristicMode = 0;      // 0=关, 1=仅记录, 2=自动拦截
    inline int HeuristicThreshold = 70; // 触发拦截的分数阈值

    inline std::wstring LogPath()
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path);
        auto pos = p.find_last_of(L"\\/");
        p = p.substr(0, pos + 1) + L"blocklog.txt";
        return p;
    }

    inline std::wstring Lower(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
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

    inline void EnsureDefaultRules()
    {
        if (AppSettings::ReadInt(L"Blocker", L"Initialized", 0) == 1) return;

        if (AppSettings::ReadInt(L"Blocker", L"RuleCount", 0) == 0)
        {
            static const wchar_t* defaults[] = {
                L"B:exe:contains:flashcenter.exe",
                L"B:exe:contains:minipage.exe",
                L"B:title:contains:热点",
                L"W:exe:contains:explorer.exe"
            };
            int i = 0;
            for (auto d : defaults)
                AppSettings::WriteString(L"Blocker",
                    (L"Rule" + std::to_wstring(i++)).c_str(), d);
            AppSettings::WriteInt(L"Blocker", L"RuleCount", i);
        }
        AppSettings::WriteInt(L"Blocker", L"Initialized", 1);
    }

    inline void SyncFromSettings()
    {
        ForceBlock = AppSettings::ReadInt(L"Blocker", L"ForceBlock", 0) == 1;
        HeuristicMode = AppSettings::ReadInt(L"Blocker", L"HeuristicMode", 0);
        HeuristicThreshold = AppSettings::ReadInt(L"Blocker", L"HeuristicThreshold", 70);

        std::lock_guard lock(RulesMutex);
        Rules.clear();
        int count = AppSettings::ReadInt(L"Blocker", L"RuleCount", 0);
        for (int i = 0; i < count; ++i)
        {
            std::wstring line = AppSettings::ReadString(
                L"Blocker", (L"Rule" + std::to_wstring(i)).c_str());

            Rule r;
            size_t p1 = line.find(L':');
            if (p1 == std::wstring::npos) continue;

            std::wstring first = line.substr(0, p1);
            size_t p2 = line.find(L':', p1 + 1);

            if (first == L"B" || first == L"W") {
                r.isWhitelist = (first == L"W");
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

                if (f_str == L"exe") r.field = RuleField::Exe;
                else if (f_str == L"path") r.field = RuleField::Path;
                else if (f_str == L"title") r.field = RuleField::Title;
                else if (f_str == L"class") r.field = RuleField::Class;
                else continue;

                if (m_str == L"exact") r.mode = MatchMode::Exact;
                else if (m_str == L"wildcard") r.mode = MatchMode::Wildcard;
                else r.mode = MatchMode::Contains;
                r.pattern = Lower(p_str);
            }
            else {
                r.isWhitelist = false;
                if (first == L"exe") r.field = RuleField::Exe;
                else if (first == L"title") r.field = RuleField::Title;
                else if (first == L"class") r.field = RuleField::Class;
                else continue;
                r.mode = MatchMode::Contains;
                r.pattern = Lower(line.substr(p1 + 1));
            }

            if (!r.pattern.empty()) Rules.push_back(r);
        }
    }

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
                L"searchui.exe",          // Win10 搜索
                L"searchhost.exe",        // Win11 搜索
                L"searchapp.exe",         // Win10 新版搜索
                L"lockapp.exe",           // 锁屏
                L"applicationframehost.exe", L"backgroundtaskhost.exe",
                L"runtimebroker.exe",     // UWP 权限提示
                L"credentialuibroker.exe",// 凭据/密码弹窗
                L"consent.exe",           // UAC
                L"peopleexperiencehost.exe", // Win10“联系人”

                // 登录与安全
                L"winlogon.exe", L"logonui.exe",
                L"smartscreen.exe",
                L"securityhealthsystray.exe", // Windows 安全中心托盘

                // 输入法与辅助功能
                L"ctfmon.exe", L"textinputhost.exe",
                L"tabtip.exe",  // 触摸键盘
                L"osk.exe",     // 屏幕键盘
                L"narrator.exe", L"magnify.exe", L"sethc.exe", L"utilman.exe",

                // 系统工具与对话框
                L"taskmgr.exe",
                L"systemsettings.exe",        // 设置
                L"systemsettingsbroker.exe",  // Win11 设置
                L"control.exe",               // 控制面板
                L"mmc.exe",                   // 管理控制台
                L"openwith.exe",              // “打开方式”
                L"msiexec.exe",               // 安装程序
                L"sndvol.exe",                // 音量合成器
                L"snippingtool.exe", L"screensketch.exe", // 截图
                L"mstsc.exe",                 // 远程桌面
                L"conhost.exe",               // 控制台窗口宿主

                // Win11 小组件
                L"widgets.exe", L"widgetservice.exe",
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

        inline int CalculateSuspicionScore(HWND hwnd) {
            int score = 0;
            LONG style = ::GetWindowLongW(hwnd, GWL_STYLE);
            LONG ex = ::GetWindowLongW(hwnd, GWL_EXSTYLE);
            if (::GetWindow(hwnd, GW_OWNER)) score += 20;
            if (ex & WS_EX_TOOLWINDOW) score += 30;
            if (ex & WS_EX_TOPMOST) score += 20;
            if (!(style & WS_THICKFRAME) && !(style & WS_MINIMIZEBOX)) score += 20;
            if (GetTitle(hwnd).empty()) score += 10;
            return score;
        }

        inline void Log(std::wstring const& s) {
            std::wstring p = LogPath();
            bool isNew = (::GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES);
            SYSTEMTIME st{}; ::GetLocalTime(&st);
            WCHAR ts[32]{}; swprintf_s(ts, L"%04d-%02d-%02d %02d:%02d:%02d ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            std::wstring full = ts + s;
            FILE* f{};
            if (_wfopen_s(&f, p.c_str(), L"ab") == 0 && f) {
                if (isNew) ::fwrite("\xEF\xBB\xBF", 1, 3, f);
                int need = ::WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (need > 0) {
                    std::string utf8(static_cast<size_t>(need) - 1, '\0');
                    ::WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, utf8.data(), need, nullptr, nullptr);
                    utf8 += "\r\n"; ::fwrite(utf8.data(), 1, utf8.size(), f);
                }
                ::fclose(f);
            }
        }

        inline void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD)
        {
            if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
            if (!::IsWindowVisible(hwnd)) return;
            if (::GetAncestor(hwnd, GA_ROOT) != hwnd) return;
            if (IsProtected(hwnd)) return;

            int matchResult = Match(hwnd);
            if (matchResult == 1) return; // 白名单放行

            bool isPopup = LooksLikePopup(hwnd);
            bool shouldBlock = false;
            std::wstring reason = L"none";

            if (matchResult == 2) {
                if (ForceBlock || isPopup) { shouldBlock = true; reason = L"blacklist"; }
            }
            else {
                if (HeuristicMode > 0) {
                    int score = CalculateSuspicionScore(hwnd);
                    if (score >= HeuristicThreshold) {
                        reason = L"heuristic(" + std::to_wstring(score) + L")";
                        if (HeuristicMode == 2 && isPopup) shouldBlock = true;
                    }
                }
            }

            if (reason != L"none") {
                Log(L"action=" + std::wstring(shouldBlock ? L"block" : L"monitor") +
                    L" | reason=" + reason + L" | title=" + GetTitle(hwnd) +
                    L" | class=" + GetClass(hwnd) + L" | exe=" + GetProcessName(hwnd));
            }

            if (shouldBlock) {
                ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
                ::ShowWindow(hwnd, SW_HIDE);
            }
        }

        inline DWORD WINAPI ThreadMain(LPVOID) {
            HookShow = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            HookFg = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            MSG msg; while (::GetMessageW(&msg, nullptr, 0, 0) > 0) { ::TranslateMessage(&msg); ::DispatchMessageW(&msg); }
            if (HookShow) ::UnhookWinEvent(HookShow); if (HookFg) ::UnhookWinEvent(HookFg);
            HookShow = HookFg = nullptr; return 0;
        }
    }

    inline void Start() { if (Running.exchange(true)) return; InitSelfExe(); detail::Worker = std::thread([] { detail::ThreadMain(nullptr); }); }
    inline void Stop() { if (!Running.exchange(false)) return; ::PostThreadMessageW(::GetThreadId(detail::Worker.native_handle()), WM_QUIT, 0, 0); if (detail::Worker.joinable()) detail::Worker.join(); }
}