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
    void ShowMainWindow()
    {
        HWND hwnd = TrayIcon::Hwnd;
        if (!hwnd) return;

        if (::IsIconic(hwnd)) ::ShowWindow(hwnd, SW_RESTORE);
        TrayIcon::Restore();

        HWND fg = ::GetForegroundWindow();
        DWORD fgThread = fg ? ::GetWindowThreadProcessId(fg, nullptr) : 0;
        DWORD cur = ::GetCurrentThreadId();
        if (fgThread && fgThread != cur) {
            ::AttachThreadInput(cur, fgThread, TRUE);
            ::BringWindowToTop(hwnd);
            ::SetForegroundWindow(hwnd);
            ::AttachThreadInput(cur, fgThread, FALSE);
        }
        else {
            ::SetForegroundWindow(hwnd);
        }
        ::FlashWindow(hwnd, FALSE);
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

            auto args = AppInstance::GetCurrent().GetActivatedEventArgs();
            m_keyInstance.RedirectActivationToAsync(args).get();
            Exit();
            return;
        }

        m_keyInstance.Activated([](IInspectable const&, AppActivationArguments const&)
            {
                auto app = Application::Current().try_as<winrt::winui::implementation::App>();
                if (!app || !app->window) return;
                app->window.DispatcherQueue().TryEnqueue([]() { ShowMainWindow(); });
            });

        window = make<MainWindow>();
        window.Activate();
    }
}