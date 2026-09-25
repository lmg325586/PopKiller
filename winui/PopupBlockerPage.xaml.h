#pragma once

#include "PopupBlockerPage.g.h"
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include "OwnerFunction.h"   // owner_function：携带注册者指针的回调槽（全局唯一定义）
#include "PopupBlocker.h"

namespace winrt::winui::implementation
{
    struct PopupBlockerPage : PopupBlockerPageT<PopupBlockerPage>
    {
        PopupBlockerPage();
        ~PopupBlockerPage();

        void EnableToggle_Toggled(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddRule_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void DeleteRule_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Pick_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SearchInput_TextChanged(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void CommunityRulesToggle_Toggled(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void RetryFetchButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void EditRule_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OpenIO_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

        winrt::Microsoft::UI::Xaml::DispatcherTimer m_statusTimer{ nullptr };
        void StatusTimer_Tick(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Windows::Foundation::IInspectable const& e);
        winrt::Microsoft::UI::Xaml::Controls::Button m_resumeButton{ nullptr };
        void RefreshStatus();
        void ResumeButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        void OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void OnNavigatedFrom(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);

        // 由 MainWindow 常驻回调在 UI 线程调用：外部（托盘/后台）状态发生变化时，
        // 从设置与引擎重新同步本页面 UI，避免"UI 状态与实际拦截状态不一致"。
        void OnExternalStateChanged();

        void RuleItem_RightTapped(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& args);
        void RuleItem_DoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);

        void RestoreCommunity_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void UpdateCommunityRestoreButtonVisibility();

    private:
        void UpdateCommunityStatus(bool ok, std::wstring const& msg);
        void RefreshList();
        void Save();
        void ReloadRulesFromEngine();

        struct RuleItem {
            int listType{ 0 };
            int fieldType{ 0 };
            int matchMode{ 0 };
            std::wstring pattern;
            bool fromCommunity{ false };
        };

        PopupBlocker::Rule ToEngineRule(RuleItem const& it);

        winrt::fire_and_forget OpenEditDialog(size_t real);

        bool m_initialized{ false };

        // 本页面注册回调时使用的 owner 令牌（页面 IInspectable 的 abi() 指针）。
        // MainWindow 常驻回调的 owner 为 MainWindow*，两者互不误伤：
        // 析构/卸载/离开页面时经 ClearCallbackIfOwnedBy 只摘除自己注册的那份。
        const void* m_callbackOwnerToken{ nullptr };


        std::vector<RuleItem> m_rules;
        std::wstring m_searchText;
        std::vector<size_t> m_visibleIndex;

        size_t m_rightClickRealIndex{ (size_t)-1 };

    };
}

namespace winrt::winui::factory_implementation
{
    struct PopupBlockerPage : PopupBlockerPageT<PopupBlockerPage, implementation::PopupBlockerPage>
    {
    };
}