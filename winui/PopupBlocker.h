#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>

#include "AppSettings.h"

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

        inline bool Match(HWND hwnd)
        {
            std::lock_guard lock(RulesMutex);
            if (Rules.empty()) return false;

            std::wstring exe, title, cls;
            for (auto const& r : Rules)
            {
                switch (r.type)
                {
                case RuleType::Exe:
                    if (exe.empty()) exe = GetProcessName(hwnd);
                    if (exe.find(r.pattern) != std::wstring::npos) return true;
                    break;
                case RuleType::Title:
                    if (title.empty()) title = GetTitle(hwnd);
                    if (title.find(r.pattern) != std::wstring::npos) return true;
                    break;
                case RuleType::Class:
                    if (cls.empty()) cls = GetClass(hwnd);
                    if (cls.find(r.pattern) != std::wstring::npos) return true;
                    break;
                }
            }
            return false;
        }

        inline void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd,
            LONG idObject, LONG idChild, DWORD, DWORD)
        {
            if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
            if (!::IsWindowVisible(hwnd)) return;
            if (::GetAncestor(hwnd, GA_ROOT) != hwnd) return;

            if (Match(hwnd))
            {
                ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
                ::ShowWindow(hwnd, SW_HIDE);
            }
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