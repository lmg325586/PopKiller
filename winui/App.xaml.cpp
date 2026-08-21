#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "TrayIcon.h"
#include <winrt/Microsoft.Windows.AppLifecycle.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace winrt::Microsoft::Windows::AppLifecycle;

namespace
{
    void ActivateFirstInstance()
    {
        WCHAR path[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring self(path);
        auto pos = self.find_last_of(L"\\/");
        if (pos != std::wstring::npos) self = self.substr(pos + 1);

        DWORD selfPid = ::GetCurrentProcessId();
        struct Ctx { std::wstring const* self; DWORD selfPid; HWND found; };
        Ctx ctx{ &self, selfPid, nullptr };

        ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            Ctx& c = *reinterpret_cast<Ctx*>(lp);
            DWORD pid{};
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (pid == 0 || pid == c.selfPid) return TRUE;
            HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) return TRUE;
            WCHAR p[MAX_PATH]{};
            DWORD sz = MAX_PATH;
            bool same = false;
            if (::QueryFullProcessImageNameW(h, 0, p, &sz)) {
                std::wstring ep(p);
                auto pos2 = ep.find_last_of(L"\\/");
                if (pos2 != std::wstring::npos) ep = ep.substr(pos2 + 1);
                same = ::_wcsicmp(ep.c_str(), c.self->c_str()) == 0;
            }
            ::CloseHandle(h);
            if (same) { c.found = hwnd; return FALSE; }
            return TRUE;
            }, reinterpret_cast<LPARAM>(&ctx));

        if (!ctx.found) return;

        ::PostMessageW(ctx.found, TrayIcon::WM_TRAYICON, 0, WM_LBUTTONDBLCLK);

        ::Sleep(150);
        HWND fg = ::GetForegroundWindow();
        DWORD fgThread = fg ? ::GetWindowThreadProcessId(fg, nullptr) : 0;
        DWORD cur = ::GetCurrentThreadId();
        if (fgThread && fgThread != cur) {
            ::AttachThreadInput(cur, fgThread, TRUE);
            ::BringWindowToTop(ctx.found);
            ::SetForegroundWindow(ctx.found);
            ::AttachThreadInput(cur, fgThread, FALSE);
        }
        else {
            ::SetForegroundWindow(ctx.found);
        }
        ::FlashWindow(ctx.found, FALSE);
    }
}

namespace winrt::winui::implementation
{
    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
            {
                if (IsDebuggerPresent())
                {
                    auto errorMessage = e.Message();
                    __debugbreak();
                }
            });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        m_keyInstance = AppInstance::FindOrRegisterForKey(L"PopKiller_Main");
        if (!m_keyInstance.IsCurrent())
        {
            ActivateFirstInstance();
            Exit();
            return;
        }

        window = make<MainWindow>();
        window.Activate();
    }
}