#pragma once

#include "LicensePage.g.h"

namespace winrt::winui::implementation
{
    struct LicensePage : LicensePageT<LicensePage>
    {
        LicensePage();

        void BackButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
    };
}

namespace winrt::winui::factory_implementation
{
    struct LicensePage : LicensePageT<LicensePage, implementation::LicensePage>
    {
    };
}