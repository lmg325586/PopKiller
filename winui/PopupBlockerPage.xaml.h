#pragma once

#include "PopupBlockerPage.g.h"
#include <string>
#include <vector>

namespace winrt::winui::implementation
{
    struct PopupBlockerPage : PopupBlockerPageT<PopupBlockerPage>
    {
        PopupBlockerPage();

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
        void CommunityRulesToggle_Toggled(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void RefreshList();
        void Save();
        bool m_initialized{ false };
        struct RuleItem {
            int listType;
            int fieldType;
            int matchMode;
            std::wstring pattern;
        };
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