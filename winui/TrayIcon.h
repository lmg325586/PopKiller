#pragma once
#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <vector>
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
        HMODULE hm = ::GetModuleHandleW(nullptr);
        HICON ic = hm ? ::ExtractIconW(hm, path, 0) : nullptr;
        if (!ic) ic = ::LoadIconW(nullptr, IDI_APPLICATION);
        return ic;
    }

    inline HICON MakeGrayIcon(HICON src)
    {
        ICONINFO ii{};
        if (!::GetIconInfo(src, &ii)) return nullptr;

        BITMAP bm{};
        int w = ::GetSystemMetrics(SM_CXSMICON), h = ::GetSystemMetrics(SM_CYSMICON);
        if (::GetObjectW(ii.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0) { w = (int)bm.bmWidth; h = (int)bm.bmHeight; }

        HDC hdcScreen = ::GetDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP hColor = ::CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HICON result = nullptr;
        if (hColor && bits) {
            HDC hdc = ::CreateCompatibleDC(hdcScreen);
            HBITMAP old = (HBITMAP)::SelectObject(hdc, hColor);
            ::DrawIconEx(hdc, 0, 0, src, w, h, 0, nullptr, DI_NORMAL);
            ::SelectObject(hdc, old);
            ::DeleteDC(hdc);

            BYTE* px = (BYTE*)bits;
            int n = w * h;
            bool anyAlpha = false;
            for (int i = 0; i < n; ++i) {
                BYTE b = px[i * 4 + 0], g = px[i * 4 + 1], r = px[i * 4 + 2];
                BYTE y = (BYTE)((r * 77 + g * 150 + b * 29) >> 8);
                px[i * 4 + 0] = y; px[i * 4 + 1] = y; px[i * 4 + 2] = y;
                if (px[i * 4 + 3]) anyAlpha = true;
            }

            if (!anyAlpha && ii.hbmMask) {
                int stride = ((w + 31) / 32) * 4;
                std::vector<BYTE> mb((size_t)stride * h);
                if (::GetBitmapBits(ii.hbmMask, (LONG)mb.size(), mb.data()) == (LONG)mb.size()) {
                    for (int yy = 0; yy < h; ++yy)
                        for (int xx = 0; xx < w; ++xx)
                            if (!((mb[yy * stride + xx / 8] >> (7 - xx % 8)) & 1))
                                px[(yy * w + xx) * 4 + 3] = 255;
                }
            }

            ICONINFO ni{};
            ni.fIcon = TRUE;
            ni.hbmMask = ii.hbmMask;
            ni.hbmColor = hColor;
            result = ::CreateIconIndirect(&ni);
        }

        if (hColor) ::DeleteObject(hColor);
        if (ii.hbmColor) ::DeleteObject(ii.hbmColor);
        if (ii.hbmMask) ::DeleteObject(ii.hbmMask);
        ::ReleaseDC(nullptr, hdcScreen);
        return result;
    }

    inline HICON IconNormal{};
    inline HICON IconGray{};

    inline void EnsureIcons()
    {
        if (!IconNormal) IconNormal = GetIcon();
        if (!IconGray && IconNormal) IconGray = MakeGrayIcon(IconNormal);
    }

    inline void UpdateTrayState()
    {
        if (!Visible) return;
        EnsureIcons();

        bool paused = PopupBlocker::Paused.load();
        bool enabled = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
        bool active = enabled && !paused;

        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = Hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_TIP | NIF_ICON;
        nid.hIcon = active ? IconNormal : IconGray;

        if (paused) {
            wcscpy_s(nid.szTip, L"PopKiller (暂停拦截中...)");
        }
        else {
            wcscpy_s(nid.szTip, enabled ? L"PopKiller (运行中)" : L"PopKiller (已关闭)");
        }

        ::Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    inline void Add()
    {
        if (Visible) return;
        EnsureIcons();

        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = Hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;

        bool enabled = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
        bool active = enabled && !PopupBlocker::Paused.load();
        nid.hIcon = active ? IconNormal : IconGray;
        wcscpy_s(nid.szTip, active ? L"PopKiller (运行中)" : L"PopKiller (已关闭)");

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

        UpdateTrayState();
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
                    UpdateTrayState();
                }
                else if (cmd == IDM_RESUME) {
                    PopupBlocker::ResumeNow();
                    UpdateTrayState();
                }
            }
            return true;
        }
        return false;
    }

    inline void Init(HWND hwnd) { Hwnd = hwnd; }
}