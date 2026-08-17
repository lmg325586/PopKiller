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
        wcscpy_s(nid.szTip, L"winui");
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
            PopupBlocker::Stop();
            AppSettings::WriteInt(L"Blocker", L"Enabled", 0);
        }
        else
        {
            PopupBlocker::SyncFromSettings();
            PopupBlocker::Start();
            AppSettings::WriteInt(L"Blocker", L"Enabled", 1);
        }

        if (PopupBlocker::EnabledChangedCallback)
            PopupBlocker::EnabledChangedCallback();
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
                ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                ::AppendMenuW(menu, MF_STRING, IDM_SHOW, L"显示主窗口");
                ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                ::AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");
                int cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN,
                    pt.x, pt.y, 0, Hwnd, nullptr);
                ::DestroyMenu(menu);

                if (cmd == IDM_TOGGLE) ToggleBlocker();
                else if (cmd == IDM_SHOW) Restore();
                else if (cmd == IDM_EXIT) ::PostMessageW(Hwnd, WM_CLOSE, 0, 0);
            }
            return true;
        }
        return false;
    }

    inline void Init(HWND hwnd) { Hwnd = hwnd; }
}