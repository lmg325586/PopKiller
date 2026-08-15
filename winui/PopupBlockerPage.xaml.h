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

    private:
        void RefreshList();
        void Save();
        std::vector<std::pair<int, std::wstring>> m_rules; // {类型下标, 模式}
    };
}

namespace winrt::winui::factory_implementation
{
    struct PopupBlockerPage : PopupBlockerPageT<PopupBlockerPage, implementation::PopupBlockerPage>
    {
    };
}