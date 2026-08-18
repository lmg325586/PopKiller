#include "pch.h"
#include "SettingsPage.xaml.h"
#include "App.xaml.h"
#include "AppTheme.h"
#include "AppSettings.h"
#include "LicensePage.xaml.h"
#include "PopupBlocker.h"
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
        HeuristicToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"HeuristicMode", 0) > 0);
        ThemeComboBox().SelectedIndex(AppTheme::Index);
        ForceBlockToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"ForceBlock", 0) == 1);
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

    void SettingsPage::ForceBlockToggle_Toggled(IInspectable const& sender, RoutedEventArgs const&)
    {
        bool on = sender.as<Controls::ToggleSwitch>().IsOn();
        AppSettings::WriteInt(L"Blocker", L"ForceBlock", on ? 1 : 0);
        PopupBlocker::ForceBlock = on;
    }

    void SettingsPage::HeuristicToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        bool on = HeuristicToggle().IsOn();
        // 开启时设为 2（自动拦截），关闭时设为 0（完全停用）。
        // 注：如果需要“仅记录”模式(1)，可以在 PopupBlockerPage 中做更细致的配置。
        AppSettings::WriteInt(L"Blocker", L"HeuristicMode", on ? 2 : 0);

        // 实时同步配置到拦截引擎
        PopupBlocker::SyncFromSettings();
    }
}