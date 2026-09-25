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
#include "HeuristicML.h"
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
    LRESULT CALLBACK MinSizeSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp,
        UINT_PTR, DWORD_PTR)
    {
        if (TrayIcon::Handle(msg, wp, lp)) return 0;

        if (msg == WM_GETMINMAXINFO)
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            UINT dpi = ::GetDpiForWindow(h);
            mmi->ptMinTrackSize.x = ::MulDiv(640, dpi, 96);
            mmi->ptMinTrackSize.y = ::MulDiv(480, dpi, 96);
            return 0;
        }

        // 接收第二实例转发的 Toast 动作
        if (msg == WM_COPYDATA)
        {
            auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
            if (cds && cds->dwData == 0x504B544F && cds->lpData)
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
                        std::wstring xml = L"<toast><visual><binding template=\"ToastGeneric\">"
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
    namespace
    {
        // 常驻（MainWindow 层）回调的 owner 令牌：即 MainWindow 实现对象地址。
        // winrt 中 get_self<impl>(abi_arg)<interface> 只是对同一指数的静态转换，
        // 因此该值与注册时写入槽位的 owner 一致，可用于精确比对。
        inline const void* MainWindowOwnerToken(winrt::winui::implementation::MainWindow* self) noexcept
        {
            return static_cast<const void*>(self);
        }
    }


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

    // 常驻（MainWindow 层）回调入口：以"自由函数 + owner(MainWindow*)"形式注册，
    // 只同步设置与引擎状态、不依赖任何页面实例，因此用户离开 PopupBlockerPage 后
    // 托盘切换开关 / 社区规则后台更新仍会生效，重新进入页面时 UI 不会出现状态漂移。
    void PersistentEnabledChanged(void* ctx)
    {
        auto* self = static_cast<winrt::winui::implementation::MainWindow*>(ctx);
        if (self) self->OnEnabledChangedPersistent();
    }

    void PersistentCommunityRulesFetched(void* ctx)
    {
        auto* self = static_cast<winrt::winui::implementation::MainWindow*>(ctx);
        if (self) self->OnCommunityRulesFetched();
    }

    // 退出清理必须持有 CallbackMutex：引擎线程/托盘线程可能正在 SafeInvoke
    // 持锁读取这些全局回调，无锁写入构成数据竞争（use-after-free 风险）。
    // 先置 ShuttingDown，令各回调入口尽早短路，再持锁摘除。
    void ClearAllCallbacksForShutdown()
    {
        PopupBlocker::ShuttingDown = true;
        std::lock_guard lock(PopupBlocker::CallbackMutex);
        PopupBlocker::EnabledChangedCallback = nullptr;
        PopupBlocker::CommunityRulesFetchCallback = nullptr;
        PopupBlocker::BlockOccurredCallback = nullptr;
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
        Title(L"PopKiller");

        this->AppWindow().Closing({ this, &MainWindow::HandleCloseRequested });

        this->Closed([this](auto&&, auto&&)
            {
                ClearAllCallbacksForShutdown();

                TrayIcon::OnExitRequested = nullptr;
                TrayIcon::OnHideToTray = nullptr;
                TrayIcon::OnRestoreFromTray = nullptr;
                TrayIcon::Remove();
                TrayIcon::Init(nullptr);
                WindowPicker::Cancel();
                PopupBlocker::Stop();

            });

        auto titleBar = this->AppWindow().TitleBar();
        titleBar.IconShowOptions(winrt::Microsoft::UI::Windowing::IconShowOptions::HideIconAndSystemMenu);
        AppTheme::Index = AppSettings::ReadInt(L"UI", L"Material", 0);
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppTheme::TitleBarElement = AppTitleBar();
        AppTheme::TitleBarElement = AppTitleBar();
        AppTheme::ApplyTitleBar(titleBar);
        AppTheme::ApplyTitleBar(titleBar);

        AppTitleBar().ActualThemeChanged([titleBar](auto&&, auto&&) {
            AppTheme::ApplyTitleBar(titleBar);
            });

        if (AppTheme::Index == 1)
        {
            SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());
        }

        PopupBlocker::EnsureDefaultRules();
        if (AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1)
        {
            PopupBlocker::SyncFromSettings();
            PopupBlocker::Start();
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
                    ::MulDiv(900, dpi, 96),
                    ::MulDiv(650, dpi, 96) });

                ::SetWindowSubclass(hwnd, MinSizeSubclass, 0, 0);
                TrayIcon::Init(hwnd);

                auto weakThis = get_weak();

                TrayIcon::OnExitRequested = [weakThis]()
                    {
                        if (auto self = weakThis.get())
                        {
                            // The tray callback runs inside the native window
                            // procedure. Defer destruction until that message
                            // has unwound so the subclass cannot be torn down
                            // while it is still executing.
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

                        if (AppTheme::Index == 1)
                            self->SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());

                        auto item = self->NavView().SelectedItem().try_as<NavigationViewItem>();
                        hstring tag;
                        if (item)
                        {
                            if (auto t = item.Tag().try_as<hstring>()) tag = *t;
                        }

                        if (tag == L"Blocker")
                            self->ContentFrame().Navigate(xaml_typename<winrt::winui::PopupBlockerPage>());
                        else if (tag == L"BlockLog")
                            self->ContentFrame().Navigate(xaml_typename<winrt::winui::BlockLogPage>());
                        else if (tag == L"Home")
                            self->ContentFrame().Navigate(xaml_typename<winrt::winui::HomePage>());
                        else
                            self->ContentFrame().Navigate(xaml_typename<winrt::winui::SettingsPage>());
                    };
            }
        }

        PopupBlocker::BlockOccurredCallback = [](std::wstring const& exe, std::wstring const& title, int matchResult) {
            if (!PopupBlocker::ToastNotify) return;

            {
                static std::mutex s_mtx;
                static std::map<std::wstring, std::chrono::steady_clock::time_point> s_lastPerExe;
                static std::chrono::steady_clock::time_point s_lastGlobal{};
                auto now = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lock(s_mtx);
                if (now - s_lastGlobal < std::chrono::seconds(3)) return;
                auto it = s_lastPerExe.find(exe);
                if (it != s_lastPerExe.end() && now - it->second < std::chrono::seconds(60)) return;
                s_lastGlobal = now;
                s_lastPerExe[exe] = now;
            }

            try {
                std::wstring toastTitle = (matchResult == 2) ? L"已拦截黑名单弹窗" : L"拦截弹窗";
                std::wstring actionsXml;

                if (matchResult == 2) {

                    actionsXml = L"<actions>"
                        L"<action content=\"查看日志\" activationType=\"protocol\" arguments=\"popkiller://toast?action=log\"/>"
                        L"</actions>";
                }
                else {

                    actionsXml = L"<actions>"
                        L"<action content=\"加入白名单\" activationType=\"protocol\" arguments=\"popkiller://toast?action=whitelist&amp;exe=" + XmlEscape(exe) + L"\"/>"
                        L"<action content=\"查看日志\" activationType=\"protocol\" arguments=\"popkiller://toast?action=log\"/>"
                        L"</actions>";
                }

                std::wstring xml = L"<toast launch=\"popkiller://toast?action=log\" activationType=\"protocol\">"
                    L"<visual><binding template=\"ToastGeneric\">"
                    L"<text>" + toastTitle + L"</text>"
                    L"<text>进程：" + XmlEscape(exe) + L"</text>";
                if (!title.empty() && title != L" ") {
                    xml += L"<text>标题：" + XmlEscape(title) + L"</text>";
                }
                xml += L"</binding></visual>" + actionsXml + L"</toast>";

                winrt::Microsoft::Windows::AppNotifications::AppNotification notification{ winrt::hstring(xml) };
                try { notification.Expiration(winrt::clock::now() + std::chrono::minutes(5)); }
                catch (...) {}

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

        // App/MainWindow 层常驻回调：生命周期与主窗口一致，不随设置页销毁而丢失。
        // 用户离开 PopupBlockerPage 后，托盘切换开关 / 社区规则后台更新仍会触发这里，
        // 保证引擎状态与设置始终一致；重新进入页面时 UI 从设置/引擎重新同步。
        // owner 指针 = MainWindow 实现对象地址（winrt 实现对象即 IInspectable），
        // 与 PopupBlockerPage 析构时的 ClearCallbackIfOwnedBy 令牌体系一致、互不误伤。
        {
            std::lock_guard lock(PopupBlocker::CallbackMutex);
            owner_bind(PopupBlocker::EnabledChangedCallback,
                &PersistentEnabledChanged, MainWindowOwnerToken(this));
            owner_bind(PopupBlocker::CommunityRulesFetchCallback,
                &PersistentCommunityRulesFetched, MainWindowOwnerToken(this));
        }
    }

    // 常驻回调实现：由托盘线程或引擎后台线程经 SafeInvoke 拷贝出回调后调用（不持锁执行），
    // 此处只做"设置 -> 引擎"的状态同步，UI 相关操作统一调度回 UI 线程。
    void MainWindow::OnEnabledChangedPersistent()
    {
        if (PopupBlocker::ShuttingDown.load()) return;

        const bool on = AppSettings::ReadInt(L"Blocker", L"Enabled", 0) == 1;
        if (on)
        {
            PopupBlocker::SyncFromSettings();
            HeuristicML::GetInstance().Init();
            PopupBlocker::Start();
        }
        else
        {
            PopupBlocker::Stop();
        }

        // 回到 UI 线程刷新当前停留在设置页的开关/状态显示（页面可能已销毁，弱引用安全）
        auto weakThis = get_weak();
        DispatcherQueue().TryEnqueue([weakThis]()
            {
                if (auto self = weakThis.get())
                    self->RefreshVisibleBlockerPage();
            });
    }

    void MainWindow::OnCommunityRulesFetched()
    {
        if (PopupBlocker::ShuttingDown.load()) return;

        // 社区规则后台更新完成后，若当前正停留在设置页则刷新其列表；
        // 否则无需处理——下次进入 PopupBlockerPage 时会通过
        // ReloadRulesFromEngine() 从引擎重新同步，UI 不会出现状态漂移。
        // 注意：本函数运行在协程续体线程上，UI 操作必须调度回 UI 线程。
        auto weakThis = get_weak();
        try
        {
            DispatcherQueue().TryEnqueue([weakThis]()
                {
                    auto self = weakThis.get();
                    if (!self || PopupBlocker::ShuttingDown.load()) return;
                    self->RefreshVisibleBlockerPage();
                });
        }
        catch (...) {}
    }

    // UI 线程：若当前导航内容正是 PopupBlockerPage，则通知其从引擎/设置重新同步
    void MainWindow::RefreshVisibleBlockerPage()
    {
        try
        {
            if (auto page = ContentFrame().Content()
                    .try_as<winrt::winui::PopupBlockerPage>())
            {
                winrt::get_self<winrt::winui::implementation::PopupBlockerPage>(page)
                    ->OnExternalStateChanged();
            }
        }
        catch (...) {}
    }

    void MainWindow::HandleCloseRequested(
        winrt::Microsoft::UI::Windowing::AppWindow const&,
        winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
    {
        if (m_forceClose) return;

        int closeBehavior = AppSettings::ReadInt(L"UI", L"CloseBehavior", -1); // 默认 -1
        if (closeBehavior == 1)
        {
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
        // 退出前立即（在 UI 线程上）摘除全部全局回调并置 ShuttingDown，
        // 避免 Close()/消息循环销毁后，托盘线程或引擎后台线程再经
        // SafeInvoke 拷贝出的回调触碰已析构的 MainWindow（use-after-free）。
        // Closed 事件中的再次清理是幂等的。
        ClearAllCallbacksForShutdown();

        m_forceClose = true;
        Close();
    }

    void MainWindow::NavView_SelectionChanged(NavigationView const&,
        NavigationViewSelectionChangedEventArgs const& args)
    {
        if (args.IsSettingsSelected())
        {
            m_currentTag = L"Settings";
            ContentFrame().Navigate(xaml_typename<winrt::winui::SettingsPage>());
            return;
        }

        auto item = args.SelectedItem().try_as<NavigationViewItem>();
        if (!item)
        {
            return;
        }

        hstring tag = unbox_value<hstring>(item.Tag());
        m_currentTag = tag;
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
