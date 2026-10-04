#include "pch.h"
#include "SettingsPage.xaml.h"
#include "App.xaml.h"
#include "AppTheme.h"
#include "AutoStart.h"
#include "AppSettings.h"
#include "HeuristicML.h"
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

    // ContentDialog 首次 ShowAsync 可能缺入场动画（WinUI #8476）：ShowAsync 前显式指定默认样式。
    void ApplyDefaultDialogStyle(winrt::Microsoft::UI::Xaml::Controls::ContentDialog const& d)
    {
        if (auto res = winrt::Microsoft::UI::Xaml::Application::Current().Resources().TryLookup(
                winrt::box_value(L"DefaultContentDialogStyle")))
            if (auto style = res.try_as<winrt::Microsoft::UI::Xaml::Style>()) d.Style(style);
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
        ThemeComboBox().SelectedIndex(AppTheme::ThemeIndex);
        ForceBlockToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"ForceBlock", 0) == 1);
        MLHeuristicToggle().IsOn(PopupBlocker::MLHeuristic);
        ToastNotifyToggle().IsOn(AppSettings::ReadInt(L"Blocker", L"ToastNotify", 1) == 1);
        VersionTextBlock().Text(APP_VERSION_STRING);
        RefreshModelVersion();

        // 打开设置页时自动检查模型更新（仅在远端更新时弹确认框；离线/失败静默）
        this->Loaded([weakThis = get_weak()](auto&&, auto&&)
            {
                if (!weakThis.get()) return;
                PopupBlocker::ModelUpdateCheckCallback =
                    [weakThis](int state, std::wstring remote, std::wstring local, std::wstring msg) {
                        if (auto s = weakThis.get())
                            s->DispatcherQueue().TryEnqueue([weakThis, state, remote, local, msg]() {
                                if (auto s2 = weakThis.get()) s2->OnModelCheckResult(state, remote, local, msg);
                                });
                    };
                PopupBlocker::ModelUpdateApplyCallback =
                    [weakThis](bool ok, std::wstring msg) {
                        if (auto s = weakThis.get())
                            s->DispatcherQueue().TryEnqueue([weakThis, ok, msg]() {
                                if (auto s2 = weakThis.get()) s2->OnModelApplyResult(ok, msg);
                                });
                    };
                PopupBlocker::FetchModelUpdateCheckAsync();
            });

        m_initialized = true;
    }

    void SettingsPage::RefreshModelVersion()
    {
        std::wstring mv = HeuristicML::StaticModelVersion();
        ModelVersionTextBlock().Text(mv.empty() ? L"未找到" : mv);
    }

    void SettingsPage::CheckModelButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_manualModelCheck = true;
        ModelVersionTextBlock().Text(L"检查中…");
        PopupBlocker::FetchModelUpdateCheckAsync();
    }

    winrt::fire_and_forget SettingsPage::OnModelCheckResult(int state, std::wstring remote, std::wstring local, std::wstring msg)
    {
        auto strong = get_strong();
        RefreshModelVersion();
        bool manual = m_manualModelCheck;
        m_manualModelCheck = false;

        if (state == 1) {
            Controls::ContentDialog dlg;
            dlg.Title(box_value(L"发现新的 ML 模型"));
            dlg.Content(box_value(L"远端版本 " + remote + L"，当前 " +
                (local.empty() ? std::wstring(L"无") : local) + L"。\n是否下载并更新？"));
            dlg.PrimaryButtonText(L"更新");
            dlg.CloseButtonText(L"取消");
            dlg.DefaultButton(Controls::ContentDialogButton::Primary);
            ApplyDefaultDialogStyle(dlg);
            dlg.XamlRoot(this->XamlRoot());
            auto r = co_await dlg.ShowAsync();
            if (r == Controls::ContentDialogResult::Primary)
                PopupBlocker::ApplyModelUpdateAsync();
        }
        else if (manual) {
            std::wstring text = (state == 0)
                ? (L"已是最新版本 " + (remote.empty() ? local : remote))
                : (L"检查失败：" + (msg.empty() ? L"未知错误" : msg));
            Controls::ContentDialog dlg;
            dlg.Title(box_value(L"模型更新"));
            dlg.Content(box_value(text));
            dlg.CloseButtonText(L"确定");
            ApplyDefaultDialogStyle(dlg);
            dlg.XamlRoot(this->XamlRoot());
            co_await dlg.ShowAsync();
        }
        co_return;
    }

    winrt::fire_and_forget SettingsPage::OnModelApplyResult(bool ok, std::wstring msg)
    {
        auto strong = get_strong();
        RefreshModelVersion();
        Controls::ContentDialog dlg;
        dlg.Title(box_value(L"模型更新"));
        dlg.Content(box_value(ok ? (L"已更新到 " + msg) : (L"更新失败：" + msg)));
        dlg.CloseButtonText(L"确定");
        ApplyDefaultDialogStyle(dlg);
        dlg.XamlRoot(this->XamlRoot());
        co_await dlg.ShowAsync();
        co_return;
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

        AppTheme::ThemeIndex = ThemeComboBox().SelectedIndex();
        AppSettings::WriteInt(L"UI", L"Material", AppTheme::ThemeIndex);

        auto window = winrt::winui::implementation::App::window;
        if (!window)
        {
            return;
        }

        if (AppTheme::ThemeIndex == 1)
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
        if (!m_initialized) return;

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
        if (!m_initialized) return;

        auto toggle = sender.as<winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch>();
        bool on = toggle.IsOn();
        bool ok = on ? AutoStart::EnableAutoStart() : AutoStart::DisableAutoStart();
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
        if (!m_initialized) return;

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
