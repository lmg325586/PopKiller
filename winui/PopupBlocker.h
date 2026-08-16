#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include "AppSettings.h"
#include <cstdio>

namespace PopupBlocker
{
    enum class RuleType { Exe, Title, Class };

    struct Rule
    {
        RuleType type;
        std::wstring pattern;
    };

    inline std::vector<Rule> Rules;
    inline std::mutex RulesMutex;
    inline std::atomic<bool> Running{ false };

    inline std::wstring Lower(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
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
                L"exe:flashcenter.exe",  
                L"exe:minipage.exe",      
                L"exe:popwnd.exe",        
                L"exe:birdpaper.exe",     
                L"title:热点",
                L"title:资讯",
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
        std::lock_guard lock(RulesMutex);
        Rules.clear();
        int count = AppSettings::ReadInt(L"Blocker", L"RuleCount", 0);
        for (int i = 0; i < count; ++i)
        {
            std::wstring line = AppSettings::ReadString(
                L"Blocker", (L"Rule" + std::to_wstring(i)).c_str());
            auto pos = line.find(L':');
            if (pos == std::wstring::npos) continue;
            std::wstring t = line.substr(0, pos);
            auto p = line.substr(pos + 1);
            if (p.empty()) continue;
            RuleType rt = (t == L"exe") ? RuleType::Exe
                : (t == L"title") ? RuleType::Title : RuleType::Class;
            Rules.push_back({ rt, Lower(p) });
        }
    }

    namespace detail
    {

        inline void Log(std::wstring const& s)
        {
            WCHAR path[MAX_PATH]{};
            ::GetModuleFileNameW(nullptr, path, MAX_PATH);
            std::wstring p(path);
            auto pos = p.find_last_of(L"\\/");
            p = p.substr(0, pos + 1) + L"blocklog.txt";

            bool isNew = (::GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES);

            SYSTEMTIME st{};
            ::GetLocalTime(&st);
            WCHAR ts[32]{};
            swprintf_s(ts, L"%04d-%02d-%02d %02d:%02d:%02d ",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            std::wstring full = ts + s;

            FILE* f{};
            if (_wfopen_s(&f, p.c_str(), L"ab") == 0 && f)
            {
                if (isNew) ::fwrite("\xEF\xBB\xBF", 1, 3, f); // UTF-8 BOM
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

        inline bool IsProtected(HWND hwnd)
        {
            static const wchar_t* list[] = {
                L"explorer.exe", L"dwm.exe", L"winlogon.exe", L"logonui.exe",
                L"taskmgr.exe", L"searchui.exe", L"startmenuexperiencehost.exe",
                L"shellexperiencehost.exe", L"applicationframehost.exe",
            };
            std::wstring exe = GetProcessName(hwnd);
            for (auto p : list)
                if (exe == p) return true;
            return false;
        }

        inline std::wstring GetTitle(HWND hwnd)
        {
            WCHAR buf[256]{};
            ::GetWindowTextW(hwnd, buf, 256);
            return Lower(buf);
        }

        inline std::wstring GetClass(HWND hwnd)
        {
            WCHAR buf[256]{};
            ::GetClassNameW(hwnd, buf, 256);
            return Lower(buf);
        }

        inline int Match(HWND hwnd)
        {
            std::lock_guard lock(RulesMutex);
            if (Rules.empty()) return -1;

            std::wstring exe, title, cls;
            for (size_t i = 0; i < Rules.size(); ++i)
            {
                auto const& r = Rules[i];
                switch (r.type)
                {
                case RuleType::Exe:
                    if (exe.empty()) exe = GetProcessName(hwnd);
                    if (exe.find(r.pattern) != std::wstring::npos) return static_cast<int>(i);
                    break;
                case RuleType::Title:
                    if (title.empty()) title = GetTitle(hwnd);
                    if (title.find(r.pattern) != std::wstring::npos) return static_cast<int>(i);
                    break;
                case RuleType::Class:
                    if (cls.empty()) cls = GetClass(hwnd);
                    if (cls.find(r.pattern) != std::wstring::npos) return static_cast<int>(i);
                    break;
                }
            }
            return -1;
        }

        inline void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd,
            LONG idObject, LONG idChild, DWORD, DWORD)
        {
            if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
            if (!::IsWindowVisible(hwnd)) return;
            if (::GetAncestor(hwnd, GA_ROOT) != hwnd) return;
            if (IsProtected(hwnd)) return;

            int idx = Match(hwnd);
            if (idx < 0) return;
            if (!LooksLikePopup(hwnd)) return;

            std::wstring ruleText;
            {
                std::lock_guard lock(RulesMutex);
                if (idx < static_cast<int>(Rules.size()))
                {
                    auto const& r = Rules[idx];
                    const wchar_t* t = r.type == RuleType::Exe ? L"exe"
                        : r.type == RuleType::Title ? L"title" : L"class";
                    ruleText = std::wstring(t) + L":" + r.pattern;
                }
            }
            Log(L"rule=" + ruleText
                + L" | title=" + GetTitle(hwnd)
                + L" | class=" + GetClass(hwnd)
                + L" | exe=" + GetProcessName(hwnd));

            ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
            ::ShowWindow(hwnd, SW_HIDE);
        }

        inline DWORD WINAPI ThreadMain(LPVOID)
        {
            HookShow = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
                nullptr, WinEventProc, 0, 0,
                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            HookFg = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                nullptr, WinEventProc, 0, 0,
                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

            MSG msg;
            while (::GetMessageW(&msg, nullptr, 0, 0) > 0)
            {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }

            if (HookShow) ::UnhookWinEvent(HookShow);
            if (HookFg) ::UnhookWinEvent(HookFg);
            HookShow = HookFg = nullptr;
            return 0;
        }
    }

    inline void Start()
    {
        if (Running.exchange(true)) return;
        detail::Worker = std::thread([] { detail::ThreadMain(nullptr); });
    }

    inline void Stop()
    {
        if (!Running.exchange(false)) return;
        ::PostThreadMessageW(::GetThreadId(detail::Worker.native_handle()), WM_QUIT, 0, 0);
        if (detail::Worker.joinable()) detail::Worker.join();
    }
}