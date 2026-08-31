#pragma once

#include "HomePage.g.h"

namespace winrt::winui::implementation
{
    struct HomePage : HomePageT<HomePage>
    {
        HomePage();

        void RootPointerMoved(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void RootPointerExited(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void GoToSettings_Tapped(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& e);
        void GoToBlocker_Tapped(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args);

    private:
        void UpdateGlow(winrt::Microsoft::UI::Xaml::Controls::Border const& card,
            winrt::Microsoft::UI::Xaml::Controls::Canvas const& canvas,
            winrt::Microsoft::UI::Xaml::Shapes::Ellipse const& glow,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
    };
}

namespace winrt::winui::factory_implementation
{
    struct HomePage : HomePageT<HomePage, implementation::HomePage>
    {
    };
}