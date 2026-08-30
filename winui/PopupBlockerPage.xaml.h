#pragma once

#include "PopupBlockerPage.g.h"
#include <string>
#include <vector>
#include <chrono>
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

    private:
        void UpdateCommunityStatus(bool ok, std::wstring const& msg);
        void RefreshList();
        void Save();
        void ReloadRulesFromEngine();

        struct RuleItem {
            int listType;
            int fieldType;
            int matchMode;
            std::wstring pattern;
            bool fromCommunity{ false };
        };

        PopupBlocker::Rule ToEngineRule(RuleItem const& it);

        bool m_initialized{ false };
        int m_editingIndex{ -1 };
        std::vector<RuleItem> m_rules;
        std::wstring m_searchText;
        std::vector<size_t> m_visibleIndex;
    };
}

namespace winrt::winui::factory_implementation
{
    struct PopupBlockerPage : PopupBlockerPageT<PopupBlockerPage, implementation::PopupBlockerPage>
    {
    };
}