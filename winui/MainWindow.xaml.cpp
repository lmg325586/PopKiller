#include "pch.h"
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
        return ::DefSubclassProc(h, msg, wp, lp);
    }
}

namespace winrt::winui::implementation
{
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
        AppTheme::ApplyTitleBar(titleBar);

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
                        if (!item)
                        {
                            ContentFrame().Navigate(xaml_typename<winrt::winui::SettingsPage>());
                            return;
                        }
                        hstring tag = unbox_value<hstring>(item.Tag());
                        if (tag == L"Blocker")
                            ContentFrame().Navigate(xaml_typename<winrt::winui::PopupBlockerPage>());
                        else if (tag == L"BlockLog")
                            ContentFrame().Navigate(xaml_typename<winrt::winui::BlockLogPage>());
                        else
                            ContentFrame().Navigate(xaml_typename<winrt::winui::HomePage>());
                    };
            }
        }
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