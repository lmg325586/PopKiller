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

        this->Closed([](auto&&, auto&&)
            {
                WindowPicker::Cancel();
                PopupBlocker::Stop();

                FILE* f{};
                if (_wfopen_s(&f, PopupBlocker::LogPath().c_str(), L"wb") == 0 && f)
                    ::fclose(f);
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
                ::SetWindowSubclass(hwnd, MinSizeSubclass, 0, 0);
                TrayIcon::Init(hwnd);

                TrayIcon::OnHideToTray = [this]()
                    {
                        ContentFrame().Content(nullptr);
                        SystemBackdrop(nullptr);
                    };

                TrayIcon::OnRestoreFromTray = [this]()
                    {
                        if (AppTheme::Index == 1)
                            SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());

                        auto item = NavView().SelectedItem().try_as<NavigationViewItem>();
                        hstring tag;
                        if (item)
                        {
                            if (auto t = item.Tag().try_as<hstring>()) tag = *t;
                        }

                        if (tag == L"Blocker")
                            ContentFrame().Navigate(xaml_typename<winrt::winui::PopupBlockerPage>());
                        else if (tag == L"BlockLog")
                            ContentFrame().Navigate(xaml_typename<winrt::winui::BlockLogPage>());
                        else if (tag == L"Home")
                            ContentFrame().Navigate(xaml_typename<winrt::winui::HomePage>());
                        else
                            ContentFrame().Navigate(xaml_typename<winrt::winui::SettingsPage>());
                    };
            }
        }

        PopupBlocker::BlockOccurredCallback = [](std::wstring const& exe, std::wstring const& title, int matchResult) {
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