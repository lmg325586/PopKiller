#include "pch.h"
#include "App.xaml.h" 
#include "MainWindow.xaml.h"
#include "HomePage.xaml.h"
#include "SettingsPage.xaml.h"
#include "PopupBlockerPage.xaml.h"
#include "AppSettings.h"
#include "WindowPicker.h"
#include "BlockLogPage.xaml.h"
#include "PopupBlocker.h"
#include "TrayIcon.h"
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.Windows.AppNotifications.h>
#include <winrt/Microsoft.Windows.AppNotifications.Builder.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <chrono>
#include "AppTheme.h"
#include "winrt/Windows.UI.Xaml.Interop.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace
{
    // 最小窗口尺寸（逻辑像素）
    constexpr int kMinWinW = 640;
    constexpr int kMinWinH = 480;
    // 初始窗口尺寸（逻辑像素）
    constexpr int kInitWinW = 900;
    constexpr int kInitWinH = 650;

    // Toast 节流：全局最快 3s 一条、同一进程 60s 一条
    constexpr int kToastThrottleGlobalMs = 3000;
    constexpr int kToastThrottlePerExeMs = 60000;

    // 第二实例转发 Toast 动作的 WM_COPYDATA 标识
    constexpr ULONG_PTR kToastCopyData = 0x504B544F;

    // Toast 动作参数
    constexpr const wchar_t* kActionLog = L"action=log";
    constexpr const wchar_t* kActionWhitelist = L"action=whitelist";

    LRESULT CALLBACK MinSizeSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp,
        UINT_PTR, DWORD_PTR)
    {
        if (TrayIcon::Handle(msg, wp, lp)) return 0;

        // 系统浅/深主题切换：刷新原生菜单主题
        if (msg == WM_THEMECHANGED ||
            (msg == WM_SETTINGCHANGE && lp &&
                ::lstrcmpW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet") == 0))
        {
            DarkMode::RefreshMenus();
        }

        if (msg == WM_GETMINMAXINFO)
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            UINT dpi = ::GetDpiForWindow(h);
            mmi->ptMinTrackSize.x = ::MulDiv(kMinWinW, dpi, 96);
            mmi->ptMinTrackSize.y = ::MulDiv(kMinWinH, dpi, 96);
            return 0;
        }

        // 接收第二实例转发的 Toast 动作
        if (msg == WM_COPYDATA)
        {
            auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
            if (cds && cds->dwData == kToastCopyData && cds->lpData)
            {
                std::wstring payload(reinterpret_cast<wchar_t*>(cds->lpData));
                auto sep = payload.find(L'|');
                std::wstring action = (sep == std::wstring::npos) ? payload : payload.substr(0, sep);
                std::wstring exe = (sep == std::wstring::npos) ? std::wstring{} : payload.substr(sep + 1);

                if (action == L"log")
                {
                    if (auto window = winrt::winui::implementation::App::window)
                    {
                        if (auto mainWin = window.try_as<winui::MainWindow>())
                        {
                            winrt::get_self<winrt::winui::implementation::MainWindow>(mainWin)
                                ->NavigateToTag(L"BlockLog");
                        }
                    }
                }
                else if (action == L"whitelist" && !exe.empty())
                {
                    if (PopupBlocker::AddWhitelistExe(exe))
                    {
                        std::wstring xml = L"<toast duration=\"short\"><visual><binding template=\"ToastGeneric\">"
                            L"<text>已加入白名单</text><text>进程 " + exe + L" 的弹窗将被放行。</text>"
                            L"</binding></visual></toast>";
                        try
                        {
                            winrt::Microsoft::Windows::AppNotifications::AppNotification n{ winrt::hstring(xml) };
                            winrt::Microsoft::Windows::AppNotifications::AppNotificationManager::Default().Show(n);
                        }
                        catch (...) {}
                    }
                }
                return 1;
            }
        }

        return ::DefSubclassProc(h, msg, wp, lp);
    }
}

namespace winrt::winui::implementation
{

    std::wstring XmlEscape(std::wstring const& s)
    {
        std::wstring r;
        for (wchar_t c : s) {
            switch (c) {
            case L'&': r += L"&amp;"; break;
            case L'<': r += L"&lt;"; break;
            case L'>': r += L"&gt;"; break;
            case L'"': r += L"&quot;"; break;
            default: r += c;
            }
        }
        return r;
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
        Title(L"PopKiller");

        this->AppWindow().Closing({ this, &MainWindow::HandleCloseRequested });

        this->Closed([this](auto&&, auto&&)
            {
                BeginShutdown();
                try { winrt::Microsoft::Windows::AppNotifications::AppNotificationManager::Default().UnregisterAll(); }
                catch (...) {}

                PopupBlocker::BlockOccurredCallback = nullptr;

                TrayIcon::OnExitRequested = nullptr;
                TrayIcon::OnHideToTray = nullptr;
                TrayIcon::OnRestoreFromTray = nullptr;
                TrayIcon::Remove();
                TrayIcon::Init(nullptr);
                WindowPicker::Cancel();

            });

        auto titleBar = this->AppWindow().TitleBar();
        titleBar.IconShowOptions(winrt::Microsoft::UI::Windowing::IconShowOptions::HideIconAndSystemMenu);
        AppTheme::ThemeIndex = AppSettings::ReadInt(L"UI", L"Material", 0);
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppTheme::TitleBarElement = AppTitleBar();
        AppTheme::ApplyTitleBar(titleBar);

        AppTitleBar().ActualThemeChanged([titleBar](auto&&, auto&&) {
            AppTheme::ApplyTitleBar(titleBar);
            });

        if (AppTheme::ThemeIndex == 1)
        {
            SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());
        }

        PopupBlocker::EnsureDefaultRules();
        bool blockerEnabled = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
        if (blockerEnabled)
        {
            PopupBlocker::SyncFromSettings();
        }

        auto menuItems = NavView().MenuItems();
        if (menuItems.Size() > 0)
        {
            NavView().SelectedItem(menuItems.GetAt(0));
        }

        auto native = this->try_as<::IWindowNative>();
        if (native)
        {
            HWND hwnd{};
            if (SUCCEEDED(native->get_WindowHandle(&hwnd)) && hwnd)
            {
                UINT dpi = ::GetDpiForWindow(hwnd);
                this->AppWindow().Resize({
                    ::MulDiv(kInitWinW, dpi, 96),
                    ::MulDiv(kInitWinH, dpi, 96) });

                ::SetWindowSubclass(hwnd, MinSizeSubclass, 0, 0);
                TrayIcon::Init(hwnd);
                DarkMode::ApplyToWindow(hwnd);

                auto weakThis = get_weak();

                TrayIcon::OnExitRequested = [weakThis]()
                    {
                        if (auto self = weakThis.get())
                        {
                            // 托盘回调在原生窗口过程内执行：推迟销毁到该消息返回之后，
                            // 避免子类过程尚未执行完就被拆掉。
                            self->DispatcherQueue().TryEnqueue([weakThis]()
                                {
                                    if (auto queuedSelf = weakThis.get())
                                    {
                                        queuedSelf->ExitApplication();
                                    }
                                });
                        }
                    };

                TrayIcon::OnHideToTray = [weakThis]()
                    {
                        if (auto self = weakThis.get())
                        {
                            self->ContentFrame().Content(nullptr);
                            self->SystemBackdrop(nullptr);
                        }
                    };

                TrayIcon::OnRestoreFromTray = [weakThis]()
                    {
                        auto self = weakThis.get();
                        if (!self) return;

                        if (AppTheme::ThemeIndex == 1)
                            self->SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());

                        auto item = self->NavView().SelectedItem().try_as<NavigationViewItem>();
                        hstring tag;
                        if (item)
                        {
                            if (auto t = item.Tag().try_as<hstring>()) tag = *t;
                        }

                        if (tag == L"Home" || tag == L"Blocker" || tag == L"BlockLog")
                            self->NavigateFrameToTag(tag);
                        else
                            self->ContentFrame().Navigate(xaml_typename<winrt::winui::SettingsPage>());
                    };
            }
        }

        PopupBlocker::BlockOccurredCallback = [](std::wstring const& exe, std::wstring const& title, int matchResult) {
            if (!PopupBlocker::ToastNotify) return;

            // 节流：全局最快 3s 一条、同一进程 60s 一条，避免弹窗风暴刷屏
            {
                static std::mutex s_mtx;
                static std::map<std::wstring, std::chrono::steady_clock::time_point> s_lastPerExe;
                static std::chrono::steady_clock::time_point s_lastGlobal{};
                auto now = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lock(s_mtx);
                if (now - s_lastGlobal < std::chrono::milliseconds(kToastThrottleGlobalMs)) return;
                auto it = s_lastPerExe.find(exe);
                if (it != s_lastPerExe.end() && now - it->second < std::chrono::milliseconds(kToastThrottlePerExeMs)) return;
                s_lastGlobal = now;
                s_lastPerExe[exe] = now;
            }

            try {
                std::wstring toastTitle = (matchResult == 2) ? L"已拦截黑名单弹窗" : L"拦截弹窗";
                std::wstring actionsXml;

                if (matchResult == 2) {

                    actionsXml = std::wstring(L"<actions>"
                        L"<action content=\"查看日志\" arguments=\"") + kActionLog +
                        L"\"/></actions>";
                }
                else {

                    actionsXml = std::wstring(L"<actions>"
                        L"<action content=\"加入白名单\" arguments=\"") + kActionWhitelist + L"&amp;exe=" + XmlEscape(exe) +
                        L"\"/><action content=\"查看日志\" arguments=\"" + kActionLog + L"\"/></actions>";
                }

                std::wstring xml = std::wstring(L"<toast duration=\"short\" launch=\"") + kActionLog + L"\">"
                    L"<visual><binding template=\"ToastGeneric\">"
                    L"<text>" + toastTitle + L"</text>"
                    L"<text>进程：" + XmlEscape(exe) + L"</text>";
                if (!title.empty() && title != L" ") {
                    xml += L"<text>标题：" + XmlEscape(title) + L"</text>";
                }
                xml += L"</binding></visual>" + actionsXml + L"</toast>";

                // duration="short" 让横幅按系统短时长（默认约 5s）自动消失；
                // 勿设 AppNotification.Expiration——未来过期时间会让通知长时间停留
                winrt::Microsoft::Windows::AppNotifications::AppNotification notification{ winrt::hstring(xml) };
                winrt::Microsoft::Windows::AppNotifications::AppNotificationManager::Default().Show(notification);
            }
            catch (winrt::hresult_error const& e) {
                wchar_t buf[128]{};
                swprintf_s(buf, L"[PopKiller] Toast 发送失败: %s (0x%08X)\n",
                    e.message().c_str(), static_cast<unsigned>(e.code().value));
                ::OutputDebugStringW(buf);
            }
            catch (...) {
                ::OutputDebugStringW(L"[PopKiller] Toast 未知异常\n");
            }
            };

        // 引擎必须在 BlockOccurredCallback 注册完成后再启动，避免引擎线程
        // 与 UI 线程并发读写同一回调槽（未加锁写 vs SafeInvoke 加锁读）。
        if (blockerEnabled)
        {
            PopupBlocker::Start();
        }
    }

    void MainWindow::BeginShutdown()
    {
        // 退出链路口：在任何窗口/控件析构之前先立旗，再 join 引擎线程。
        // 幂等；ShuttingDown 一旦置位不再复位。
        if (PopupBlocker::ShuttingDown.exchange(true)) return;
        PopupBlocker::Stop();
    }

    void MainWindow::HandleCloseRequested(
        winrt::Microsoft::UI::Windowing::AppWindow const&,
        winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
    {
        if (m_forceClose)
        {
            BeginShutdown();
            return;
        }

        int closeBehavior = AppSettings::ReadInt(L"UI", L"CloseBehavior", -1); // 默认 -1
        if (closeBehavior == 1)
        {
            BeginShutdown();
            m_forceClose = true;
            return;
        }

        args.Cancel(true);

        if (closeBehavior == 2)
        {
            TrayIcon::HideToTray();
        }
        else if (!m_closeDialogOpen)
        {
            ShowCloseConfirmation();
        }
    }

    winrt::fire_and_forget MainWindow::ShowCloseConfirmation()
    {
        auto lifetime = get_strong();
        m_closeDialogOpen = true;
        try
        {
            auto content = this->Content();
            if (!content)
            {
                m_closeDialogOpen = false;
                co_return;
            }

            auto root = content.XamlRoot();
            if (!root)
            {
                m_closeDialogOpen = false;
                co_return;
            }

            bool remember = (AppSettings::ReadInt(L"UI", L"CloseBehavior", -1) == -1);

            ContentDialog dialog;
            dialog.XamlRoot(root);
            dialog.Title(box_value(L"关闭 PopKiller"));
            dialog.Content(box_value(L"点击关闭窗口按钮时，PopKiller 应如何处理？可随时在设置中更改。"));
            dialog.PrimaryButtonText(L"退出 PopKiller");
            dialog.SecondaryButtonText(L"最小化到托盘");
            dialog.CloseButtonText(L"取消");
            dialog.DefaultButton(ContentDialogButton::Secondary);

            // 修复代码创建的 ContentDialog 首次 ShowAsync 缺少入场动画（WinUI 已知问题，
            // 见 microsoft/microsoft-ui-xaml#8476）：ShowAsync 前显式指定默认样式，
            // 使模板在进入弹窗树前即定型，避免 HostDialogWithinPopup 重设 Popup.Child 取消 Load 过渡。
            if (auto res = Application::Current().Resources().TryLookup(box_value(L"DefaultContentDialogStyle")))
            {
                if (auto style = res.try_as<Style>()) dialog.Style(style);
            }

            auto result = co_await dialog.ShowAsync();
            m_closeDialogOpen = false;

            if (result == ContentDialogResult::Primary)
            {
                if (remember) AppSettings::WriteInt(L"UI", L"CloseBehavior", 1);
                ExitApplication();
            }
            else if (result == ContentDialogResult::Secondary)
            {
                if (remember) AppSettings::WriteInt(L"UI", L"CloseBehavior", 2);
                TrayIcon::HideToTray();
            }
        }
        catch (...)
        {
            m_closeDialogOpen = false;
            co_return;
        }
    }

    void MainWindow::ExitApplication()
    {
        m_forceClose = true;
        Close();
    }

    void MainWindow::NavView_SelectionChanged(NavigationView const&,
        NavigationViewSelectionChangedEventArgs const& args)
    {
        if (args.IsSettingsSelected())
        {
            ContentFrame().Navigate(xaml_typename<winrt::winui::SettingsPage>());
            return;
        }

        auto item = args.SelectedItem().try_as<NavigationViewItem>();
        if (!item)
        {
            return;
        }

        hstring tag = unbox_value<hstring>(item.Tag());
        NavigateFrameToTag(tag);
    }

    void MainWindow::NavigateFrameToTag(hstring const& tag)
    {
        if (tag == L"Home")
        {
            ContentFrame().Navigate(xaml_typename<winrt::winui::HomePage>());
        }
        else if (tag == L"Blocker")
        {
            ContentFrame().Navigate(xaml_typename<winrt::winui::PopupBlockerPage>());
        }
        else if (tag == L"BlockLog")
        {
            ContentFrame().Navigate(xaml_typename<winrt::winui::BlockLogPage>());
        }
    }

    void MainWindow::NavigateToTag(hstring const& tag)
    {
        if (tag == L"Settings")
        {
            NavView().SelectedItem(NavView().SettingsItem());
            return;
        }

        auto items = NavView().MenuItems();
        for (uint32_t i = 0; i < items.Size(); ++i)
        {
            if (auto item = items.GetAt(i).try_as<NavigationViewItem>())
            {
                auto itemTag = item.Tag().try_as<hstring>();
                if (itemTag && *itemTag == tag)
                {
                    NavView().SelectedItem(item);
                    return;
                }
            }
        }
    }
}
