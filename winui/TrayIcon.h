#pragma once
#include <windows.h>
#include <shellapi.h>
#include <functional>
#include "PopupBlocker.h"
#include "AppSettings.h"
#pragma comment(lib, "shell32.lib")

namespace TrayIcon
{
    constexpr UINT WM_TRAYICON = WM_APP + 50;
    constexpr UINT IDM_SHOW = 1;
    constexpr UINT IDM_EXIT = 2;
    constexpr UINT IDM_TOGGLE = 3;

    // 暂停菜单 ID
    constexpr UINT IDM_PAUSE_5 = 10;
    constexpr UINT IDM_PAUSE_10 = 11;
    constexpr UINT IDM_PAUSE_20 = 12;
    constexpr UINT IDM_PAUSE_30 = 13;
    constexpr UINT IDM_PAUSE_60 = 14;
    constexpr UINT IDM_RESUME = 15;

    inline HWND Hwnd{};
    inline bool Visible = false;
    inline std::function<void()> OnHideToTray;
    inline std::function<void()> OnRestoreFromTray;

    inline HICON GetIcon()
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        HICON ic = ::ExtractIconW(::GetModuleHandleW(nullptr), path, 0);
        if (!ic) ic = ::LoadIconW(nullptr, IDI_APPLICATION);
        return ic;
    }

    // 动态更新 Tooltip
    inline void UpdateTooltip()
    {
        if (!Visible) return;
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = Hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_TIP;

        if (PopupBlocker::Paused.load()) {
            wcscpy_s(nid.szTip, L"PopKiller (暂停拦截中...)");
        }
        else {
            bool on = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
            wcscpy_s(nid.szTip, on ? L"PopKiller (运行中)" : L"PopKiller (已关闭)");
        }
        ::Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    inline void Add()
    {
        if (Visible) return;
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = Hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = GetIcon();

        // 初始 Tooltip
        bool on = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
        wcscpy_s(nid.szTip, on ? L"PopKiller (运行中)" : L"PopKiller (已关闭)");

        ::Shell_NotifyIconW(NIM_ADD, &nid);
        Visible = true;
    }

    inline void Remove()
    {
        if (!Visible) return;
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = Hwnd;
        nid.uID = 1;
        ::Shell_NotifyIconW(NIM_DELETE, &nid);
        Visible = false;
    }

    inline void HideToTray()
    {
        Add();
        ::ShowWindow(Hwnd, SW_HIDE);
        if (OnHideToTray) OnHideToTray();
        ::SetProcessWorkingSetSize(::GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    }

    inline void Restore()
    {
        Remove();
        ::ShowWindow(Hwnd, SW_SHOW);
        if (OnRestoreFromTray) OnRestoreFromTray();
        ::SetForegroundWindow(Hwnd);
    }

    inline void ToggleBlocker()
    {
        bool on = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
        if (on)
        {
            // 关闭主开关时，如果有暂停状态，打断定时器线程
            if (PopupBlocker::Paused.load()) {
                PopupBlocker::Paused.store(false);
                ++PopupBlocker::PauseGen;
            }
            PopupBlocker::Stop();
            AppSettings::WriteInt(L"Blocker", L"Enabled", 0);
        }
        else
        {
            PopupBlocker::SyncFromSettings();
            HeuristicML::GetInstance().Init();
            PopupBlocker::Start();
            AppSettings::WriteInt(L"Blocker", L"Enabled", 1);
        }

        if (PopupBlocker::EnabledChangedCallback)
            PopupBlocker::EnabledChangedCallback();

        UpdateTooltip();
    }

    inline bool Handle(UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_SYSCOMMAND && (wp & 0xFFF0) == SC_MINIMIZE)
        {
            HideToTray();
            return true;
        }

        if (msg == WM_TRAYICON)
        {
            UINT ev = static_cast<UINT>(lp);
            if (ev == WM_LBUTTONUP || ev == WM_LBUTTONDBLCLK)
            {
                Restore();
            }
            else if (ev == WM_RBUTTONUP)
            {
                POINT pt{};
                ::GetCursorPos(&pt);
                ::SetForegroundWindow(Hwnd);

                bool on = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
                HMENU menu = ::CreatePopupMenu();

                ::AppendMenuW(menu, MF_STRING | (on ? MF_CHECKED : MF_UNCHECKED),
                    IDM_TOGGLE, L"弹窗拦截");

                // 子菜单：暂停拦截 (仅主开关开启时显示)
                HMENU pauseMenu = nullptr;
                if (on) {
                    pauseMenu = ::CreatePopupMenu();
                    ::AppendMenuW(pauseMenu, MF_STRING, IDM_PAUSE_5, L"5 分钟");
                    ::AppendMenuW(pauseMenu, MF_STRING, IDM_PAUSE_10, L"10 分钟");
                    ::AppendMenuW(pauseMenu, MF_STRING, IDM_PAUSE_20, L"20 分钟");
                    ::AppendMenuW(pauseMenu, MF_STRING, IDM_PAUSE_30, L"30 分钟");
                    ::AppendMenuW(pauseMenu, MF_STRING, IDM_PAUSE_60, L"60 分钟");
                    if (PopupBlocker::Paused.load()) {
                        ::AppendMenuW(pauseMenu, MF_SEPARATOR, 0, nullptr);
                        ::AppendMenuW(pauseMenu, MF_STRING, IDM_RESUME, L"立即恢复");
                    }
                    ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(pauseMenu), L"暂停拦截 ▶");
                }

                ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                ::AppendMenuW(menu, MF_STRING, IDM_SHOW, L"显示主窗口");
                ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                ::AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");

                int cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN,
                    pt.x, pt.y, 0, Hwnd, nullptr);

                // DestroyMenu 会级联销毁附加在上面的子菜单
                ::DestroyMenu(menu);

                if (cmd == IDM_TOGGLE) ToggleBlocker();
                else if (cmd == IDM_SHOW) Restore();
                else if (cmd == IDM_EXIT) ::PostMessageW(Hwnd, WM_CLOSE, 0, 0);
                else if (cmd >= IDM_PAUSE_5 && cmd <= IDM_PAUSE_60) {
                    int minutes = 0;
                    if (cmd == IDM_PAUSE_5) minutes = 5;
                    else if (cmd == IDM_PAUSE_10) minutes = 10;
                    else if (cmd == IDM_PAUSE_20) minutes = 20;
                    else if (cmd == IDM_PAUSE_30) minutes = 30;
                    else if (cmd == IDM_PAUSE_60) minutes = 60;

                    PopupBlocker::PauseForMinutes(minutes);
                    UpdateTooltip();
                }
                else if (cmd == IDM_RESUME) {
                    PopupBlocker::ResumeNow();
                    UpdateTooltip();
                }
            }
            return true;
        }
        return false;
    }

    inline void Init(HWND hwnd) { Hwnd = hwnd; }
}