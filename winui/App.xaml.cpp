#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "TrayIcon.h"
#include "PopupBlocker.h"
#include <winrt/Microsoft.Windows.AppLifecycle.h>
#include <shellapi.h>
#include <sstream>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace winrt::Microsoft::Windows::AppLifecycle;

namespace
{
    constexpr ULONG_PTR kToastCopyData = 0x504B544F;

    void RegisterToastProtocol()
    {
        WCHAR exePath[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" \"%1\"";
        const wchar_t* protoName = L"URL:PopKiller Protocol";

        HKEY root{};
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\popkiller",
            0, nullptr, 0, KEY_WRITE, nullptr, &root, nullptr) != ERROR_SUCCESS) return;

        ::RegSetKeyValueW(root, nullptr, nullptr, REG_SZ, protoName,
            static_cast<DWORD>((wcslen(protoName) + 1) * sizeof(wchar_t)));
        ::RegSetKeyValueW(root, nullptr, L"URL Protocol", REG_SZ, L"", sizeof(wchar_t));

        HKEY cmdKey{};
        if (::RegCreateKeyExW(root, L"shell\\open\\command", 0, nullptr, 0, KEY_WRITE,
            nullptr, &cmdKey, nullptr) == ERROR_SUCCESS)
        {
            ::RegSetKeyValueW(cmdKey, nullptr, nullptr, REG_SZ, cmd.c_str(),
                static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
            ::RegCloseKey(cmdKey);
        }
        ::RegCloseKey(root);
    }

    void ActivateFirstInstance(std::wstring const* toastPayload = nullptr, bool bringToFront = true)
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

        if (toastPayload) {
            if (bringToFront) {
                ::PostMessageW(ctx.found, TrayIcon::WM_TRAYICON, 0, WM_LBUTTONDBLCLK);
            }

            COPYDATASTRUCT cds{};
            cds.dwData = kToastCopyData;
            cds.cbData = static_cast<DWORD>((toastPayload->size() + 1) * sizeof(wchar_t));
            cds.lpData = const_cast<wchar_t*>(toastPayload->c_str());
            ::SendMessageW(ctx.found, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
        }
        else {
            ::PostMessageW(ctx.found, TrayIcon::WM_TRAYICON, 0, WM_LBUTTONDBLCLK);
        }

        if (!bringToFront) return;

        // 稍微多等一会，让主窗口有时间渲染和恢复
        ::Sleep(200);

        // 3. 确保从最小化状态恢复
        if (::IsIconic(ctx.found)) {
            ::ShowWindow(ctx.found, SW_RESTORE);
        }

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
        std::wstring action, exeParam;
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                std::wstring arg = argv[i];
                if (arg.find(L"popkiller://") == 0) {
                    auto q = arg.find(L'?');
                    if (q == std::wstring::npos) continue;
                    arg = arg.substr(q + 1);
                }
                else if (arg.find(L"action=") != 0) continue;

                std::wstringstream ss(arg);
                std::wstring item;
                while (std::getline(ss, item, L'&')) {
                    auto eq = item.find(L'=');
                    if (eq == std::wstring::npos) continue;
                    std::wstring key = item.substr(0, eq);
                    std::wstring val = item.substr(eq + 1);
                    if (key == L"action") action = val;
                    else if (key == L"exe") exeParam = val;
                }
            }
            ::LocalFree(argv);
        }
        bool isToastActivation = !action.empty();

        m_keyInstance = AppInstance::FindOrRegisterForKey(L"PopKiller_Main");
        if (!m_keyInstance.IsCurrent())
        {
            if (isToastActivation) {
                std::wstring payload = action + L"|" + exeParam;
                ActivateFirstInstance(&payload, action == L"log");
            }
            else {
                ActivateFirstInstance();
            }
            Exit();
            return;
        }

        RegisterToastProtocol();

        bool isAutoStart = std::wstring(::GetCommandLineW()).find(L"--autostart") != std::wstring::npos;

        window = make<MainWindow>();

        if (isToastActivation) {
            std::wstring a = action, x = exeParam;
            window.DispatcherQueue().TryEnqueue([a, x]() {
                if (a == L"log") {
                    if (auto w = winrt::winui::implementation::App::window) {
                        if (auto mw = w.try_as<winui::MainWindow>()) {
                            winrt::get_self<winrt::winui::implementation::MainWindow>(mw)->NavigateToTag(L"BlockLog");
                        }
                    }
                }
                else if (a == L"whitelist" && !x.empty()) {
                    PopupBlocker::AddWhitelistExe(x);
                }
                });
        }

        if (isAutoStart)
        {
            TrayIcon::HideToTray();
        }
        else
        {
            window.Activate();
        }
    }
}