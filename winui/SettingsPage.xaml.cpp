#include "pch.h"
#include "SettingsPage.xaml.h"
#include "App.xaml.h"
#include "AppTheme.h"
#include "AutoStart.h"
#include "AppSettings.h"
#include "LicensePage.xaml.h"
#include "PrivacyPage.xaml.h"
#include "PopupBlocker.h"
#include "winrt/Windows.UI.Xaml.Interop.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

#if __has_include("VersionInfo.h")
#include "VersionInfo.h"
#else
#define APP_VERSION_STRING L"Beta 0.8"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{

    int IndexToMode(int idx)
    {
        switch (idx) {
        case 1:  return 2;
        case 2:  return 1;
        default: return 0;
        }
    }

    int ModeToIndex(int mode)
    {
        switch (mode) {
        case 2:  return 1;
        case 1:  return 2;
        default: return 0;
        }
    }
}

namespace winrt::winui::implementation
{
    SettingsPage::SettingsPage()
    {
        InitializeComponent();
        VerboseLogToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"VerboseLog", 0) == 1);
        GameModeToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"GameMode", 0) == 1);
        int mode = AppSettings::ReadInt(L"Blocker", L"HeuristicMode", 0);
        HeuristicModeCombo().SelectedIndex(ModeToIndex(mode));
        AutoStartToggle().IsOn(AutoStart::IsEnabled());
        int closeBehavior = AppSettings::ReadInt(L"UI", L"CloseBehavior", -1);
        CloseBehaviorCombo().SelectedIndex(closeBehavior >= 0 && closeBehavior <= 2 ? closeBehavior : 0);
        ThemeComboBox().SelectedIndex(AppTheme::Index);
        ForceBlockToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"ForceBlock", 0) == 1);
        MLHeuristicToggle().IsOn(PopupBlocker::MLHeuristic);
        ToastNotifyToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"ToastNotify", 1) == 1);
        VersionTextBlock().Text(APP_VERSION_STRING);

        m_initialized = true;
    }

    void SettingsPage::LicenseLink_Click(IInspectable const&, RoutedEventArgs const&)
    {
        Frame().Navigate(xaml_typename<winrt::winui::LicensePage>());
    }

    void SettingsPage::PrivacyLink_Click(IInspectable const&, RoutedEventArgs const&)
    {
        Frame().Navigate(xaml_typename<winrt::winui::PrivacyPage>());
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

    void SettingsPage::HeuristicModeCombo_SelectionChanged(IInspectable const&,
        Controls::SelectionChangedEventArgs const&)
    {
        if (!m_initialized) return;

        int mode = IndexToMode(HeuristicModeCombo().SelectedIndex());
        AppSettings::WriteInt(L"Blocker", L"HeuristicMode", mode);

        PopupBlocker::SyncFromSettings();
    }

    void SettingsPage::VerboseLogToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_initialized) return;

        bool on = VerboseLogToggle().IsOn();
        AppSettings::WriteInt(L"Blocker", L"VerboseLog", on ? 1 : 0);

        PopupBlocker::SyncFromSettings();
    }

    void SettingsPage::GameModeToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_initialized) return;

        bool on = GameModeToggle().IsOn();
        AppSettings::WriteInt(L"Blocker", L"GameMode", on ? 1 : 0);
        PopupBlocker::GameMode = on;
    }

    void SettingsPage::AutoStartToggle_Toggled(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto toggle = sender.as<winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch>();
        bool on = toggle.IsOn();
        bool ok = on ? AutoStart::EnableAutoStartup() : AutoStart::DisableAutoStartup();
        if (!ok) toggle.IsOn(!on);
    }

    void SettingsPage::CloseBehaviorCombo_SelectionChanged(IInspectable const&,
        Controls::SelectionChangedEventArgs const&)
    {
        if (!m_initialized) return;

        int behavior = CloseBehaviorCombo().SelectedIndex();
        if (behavior >= 0 && behavior <= 2)
        {
            AppSettings::WriteInt(L"UI", L"CloseBehavior", behavior);
        }
    }

    void SettingsPage::MLHeuristicToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        bool on = MLHeuristicToggle().IsOn();
        AppSettings::WriteInt(L"Blocker", L"MLHeuristic", on ? 1 : 0);
        PopupBlocker::MLHeuristic = on;
        if (on) HeuristicML::GetInstance().Init();
    }

    void SettingsPage::ToastNotifyToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_initialized) return;
        bool on = ToastNotifyToggle().IsOn();
        AppSettings::WriteInt(L"Blocker", L"ToastNotify", on ? 1 : 0);
        PopupBlocker::ToastNotify = on;
    }
}
