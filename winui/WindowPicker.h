#pragma once
#include <windows.h>
#include <commctrl.h>
#include <functional>
#include <string>
#include "PopupBlocker.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace WindowPicker
{
    struct PickResult
    {
        std::wstring exe;
        std::wstring title;
        std::wstring className;
    };

    namespace detail
    {
        inline HWND MainHwnd{};
        inline HWND OverlayHwnd{};
        inline HWND FrameHwnd{};
        inline HWND Target{};
        inline ULONGLONG StartTick = 0;
        inline std::function<void(PickResult)> OnPicked;

        inline LRESULT CALLBACK FrameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        {
            return ::DefWindowProcW(h, msg, wp, lp);
        }

        inline void EnsureFrame()
        {
            if (FrameHwnd) return;
            WNDCLASSW wc{};
            wc.lpfnWndProc = FrameProc;
            wc.hInstance = ::GetModuleHandleW(nullptr);
            wc.lpszClassName = L"WinuiPickerFrame";
            ::RegisterClassW(&wc);
            FrameHwnd = ::CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
                WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                L"WinuiPickerFrame", nullptr, WS_POPUP | WS_DISABLED,
                0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
        }

        inline void PaintFrame()
        {
            RECT rc{};
            ::GetClientRect(FrameHwnd, &rc);
            int w = rc.right, h = rc.bottom;
            if (w <= 0 || h <= 0) return;

            HDC dcScreen = ::GetDC(nullptr);
            HDC dcMem = ::CreateCompatibleDC(dcScreen);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP bmp = ::CreateDIBSection(dcScreen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!bmp || !bits)
            {
                if (bmp) ::DeleteObject(bmp);
                ::DeleteDC(dcMem);
                ::ReleaseDC(nullptr, dcScreen);
                return;
            }
            HGDIOBJ old = ::SelectObject(dcMem, bmp);

            ::memset(bits, 0, static_cast<size_t>(w) * h * 4);
            DWORD* px = static_cast<DWORD*>(bits);
            const int t = 3;
            const DWORD blue = (255u << 24) | (215u << 16) | (120u << 8) | 0u;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (y < t || y >= h - t || x < t || x >= w - t)
                        px[y * w + x] = blue;

            RECT wr{};
            ::GetWindowRect(FrameHwnd, &wr);
            POINT dst{ wr.left, wr.top };
            SIZE size{ w, h };
            POINT src{ 0, 0 };
            BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            ::UpdateLayeredWindow(FrameHwnd, dcScreen, &dst, &size, dcMem, &src, 0, &bf, ULW_ALPHA);

            ::SelectObject(dcMem, old);
            ::DeleteObject(bmp);
            ::DeleteDC(dcMem);
            ::ReleaseDC(nullptr, dcScreen);
        }

        inline void MoveFrame()
        {
            if (!Target) { ::ShowWindow(FrameHwnd, SW_HIDE); return; }
            RECT r{};
            ::GetWindowRect(Target, &r);
            ::SetWindowPos(FrameHwnd, HWND_TOPMOST,
                r.left - 3, r.top - 3,
                (r.right - r.left) + 6, (r.bottom - r.top) + 6,
                SWP_SHOWWINDOW | SWP_NOACTIVATE);
            PaintFrame();
        }

        inline LRESULT CALLBACK OverlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

        inline void EnsureOverlay()
        {
            if (OverlayHwnd) return;
            WNDCLASSW wc{};
            wc.lpfnWndProc = OverlayProc;
            wc.hInstance = ::GetModuleHandleW(nullptr);
            wc.lpszClassName = L"WinuiPickerOverlay";
            wc.hCursor = ::LoadCursorW(nullptr, IDC_CROSS);
            ::RegisterClassW(&wc);
            int x = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
            int y = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
            int w = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
            int hh = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
            OverlayHwnd = ::CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
                L"WinuiPickerOverlay", nullptr, WS_POPUP,
                x, y, w, hh, nullptr, nullptr, wc.hInstance, nullptr);
            ::SetLayeredWindowAttributes(OverlayHwnd, 0, 1, LWA_ALPHA);
        }

        inline void Teardown(bool commit)
        {
            HWND t = Target;
            Target = nullptr;
            if (FrameHwnd) ::ShowWindow(FrameHwnd, SW_HIDE);
            if (OverlayHwnd) ::ShowWindow(OverlayHwnd, SW_HIDE);
            if (commit && t && OnPicked)
            {
                PickResult r;
                r.exe = PopupBlocker::detail::GetProcessName(t);
                r.title = PopupBlocker::detail::GetTitle(t);
                r.className = PopupBlocker::detail::GetClass(t);
                OnPicked(r);
            }
        }

        inline LRESULT CALLBACK OverlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        {
            switch (msg)
            {
            case WM_MOUSEMOVE:
            {
                POINT pt{};
                ::GetCursorPos(&pt);
                ::ShowWindow(h, SW_HIDE);
                HWND t = ::ChildWindowFromPointEx(::GetDesktopWindow(), pt,
                    CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
                ::ShowWindow(h, SW_SHOWNA);
                if (t == FrameHwnd || t == h) t = Target;
                if (t != Target)
                {
                    Target = t;
                    MoveFrame();
                }
                return 0;
            }
            case WM_LBUTTONUP:
                if (::GetTickCount64() - StartTick > 200)
                    Teardown(true);
                return 0;
            case WM_RBUTTONDOWN:
                Teardown(false);
                return 0;
            case WM_KEYDOWN:
                if (wp == VK_ESCAPE) Teardown(false);
                return 0;
            default:
                return ::DefWindowProcW(h, msg, wp, lp);
            }
        }
    }

    inline void Start(HWND mainHwnd, std::function<void(PickResult)> cb)
    {
        detail::MainHwnd = mainHwnd;
        detail::OnPicked = std::move(cb);
        if (detail::OverlayHwnd && ::IsWindowVisible(detail::OverlayHwnd)) return;
        detail::EnsureFrame();
        detail::EnsureOverlay();
        detail::Target = nullptr;
        detail::StartTick = ::GetTickCount64();
        ::ShowWindow(detail::OverlayHwnd, SW_SHOW);
        ::SetForegroundWindow(detail::OverlayHwnd);
        ::SetFocus(detail::OverlayHwnd);
    }

    inline void Cancel()
    {
        detail::Teardown(false);
    }
}