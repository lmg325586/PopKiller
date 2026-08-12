#include "pch.h"
#include "MainWindow.xaml.h"
#include "HomePage.xaml.h"
#include "SettingsPage.xaml.h"
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

        auto titleBar = this->AppWindow().TitleBar();
        titleBar.IconShowOptions(winrt::Microsoft::UI::Windowing::IconShowOptions::HideIconAndSystemMenu);

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppTheme::TitleBarElement = AppTitleBar();

        AppTheme::ApplyTitleBar(titleBar);

        if (AppTheme::Index == 1)
        {
            SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());
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
    }
}