#pragma once

#include "RuleIOPage.g.h"

namespace winrt::winui::implementation
{
    struct RuleIOPage : RuleIOPageT<RuleIOPage>
    {
        RuleIOPage() = default;

        void BackButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ExportButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ImportButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
    };
}

namespace winrt::winui::factory_implementation
{
    struct RuleIOPage : RuleIOPageT<RuleIOPage, implementation::RuleIOPage>
    {
    };
}