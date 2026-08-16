#include "pch.h"
#include "SettingsPage.xaml.h"
#include "App.xaml.h"
#include "AppTheme.h"
#include "AppSettings.h"
#include "LicensePage.xaml.h"
#include "winrt/Windows.UI.Xaml.Interop.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::winui::implementation
{
    SettingsPage::SettingsPage()
    {
        InitializeComponent();
        ThemeComboBox().SelectedIndex(AppTheme::Index);
        m_initialized = true;
    }

    void SettingsPage::LicenseLink_Click(IInspectable const&, RoutedEventArgs const&)
    {
        Frame().Navigate(xaml_typename<winrt::winui::LicensePage>());
    }

    void SettingsPage::ThemeComboBox_SelectionChanged(IInspectable const&,
        Controls::SelectionChangedEventArgs const&)
    {
        if (!m_initialized)
        {
            return;
        }

        AppTheme::Index = ThemeComboBox().SelectedIndex();
        AppSettings::WriteInt(L"UI", L"Material", AppTheme::Index);

        auto window = winrt::winui::implementation::App::window;
        if (!window)
        {
            return;
        }

        if (AppTheme::Index == 1)
        {
            window.SystemBackdrop(Media::MicaBackdrop());
        }
        else
        {
            window.SystemBackdrop(nullptr);
        }

        AppTheme::ApplyTitleBar(window.AppWindow().TitleBar());
    }
}