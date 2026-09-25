#pragma once

#include "PrivacyPage.g.h"

namespace winrt::winui::implementation
{
    struct PrivacyPage : PrivacyPageT<PrivacyPage>
    {
        PrivacyPage();

        void BackButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
    };
}

namespace winrt::winui::factory_implementation
{
    struct PrivacyPage : PrivacyPageT<PrivacyPage, implementation::PrivacyPage>
    {
    };
}
